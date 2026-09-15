#!/usr/bin/env python3
"""Train a Spark MLlib load model with a time-ordered validation split."""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path

from pyspark.ml import Pipeline
from pyspark.ml.evaluation import RegressionEvaluator
from pyspark.ml.feature import VectorAssembler
from pyspark.ml.regression import RandomForestRegressor
from pyspark.sql import SparkSession, functions as F


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--features", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--master", default="local[*]")
    args = parser.parse_args()
    spark = (SparkSession.builder.master(args.master).appName("ev-charging-load-model")
             .config("spark.sql.session.timeZone", "UTC").config("spark.ui.enabled", "false")
             # ml 链为本地链：显式 file:// 文件系统，避免宿主 Hadoop 配置把本地路径解析到 HDFS
             .config("spark.hadoop.fs.defaultFS", "file:///")
             .getOrCreate())
    spark.sparkContext.setLogLevel("WARN")
    try:
        frame = (spark.read.parquet(str(args.features))
                 .withColumn("date_ts", F.to_timestamp("date"))
                 .withColumn("label", F.col("load_kwh").cast("double"))
                 .withColumn("hour_num", F.col("start_hour").cast("double"))
                 .withColumn("station_num", F.col("station_id").cast("double"))
                 .withColumn("session_num", F.col("session_count").cast("double"))
                 .withColumn("user_num", F.col("user_count").cast("double"))
                 .withColumn("utilization_num", F.coalesce(F.col("utilization"), F.lit(0.0)))
                 .withColumn("idle_num", F.coalesce(F.col("idle_pile_estimate"), F.lit(0.0)))
                 .dropna(subset=["date_ts", "label", "hour_num", "station_num", "session_num", "user_num"]))
        ordered = frame.orderBy("date_ts", "start_hour", "station_id")
        total = ordered.count()
        if total < 20:
            raise ValueError(f"at least 20 feature rows are required, got {total}")
        split = max(1, int(total * 0.8))
        boundary = ordered.select("date_ts").collect()[split - 1][0]
        train = ordered.filter(F.col("date_ts") <= F.lit(boundary))
        test = ordered.filter(F.col("date_ts") > F.lit(boundary))
        if test.count() == 0:
            train = ordered.limit(max(1, total - 1))
            test = ordered.subtract(train)
        feature_cols = ["hour_num", "station_num", "session_num", "user_num", "utilization_num", "idle_num"]
        pipeline = Pipeline(stages=[
            VectorAssembler(inputCols=feature_cols, outputCol="features", handleInvalid="keep"),
            RandomForestRegressor(featuresCol="features", labelCol="label", numTrees=80, maxDepth=8, seed=20260912),
        ])
        model = pipeline.fit(train)
        predictions = model.transform(test)
        evaluator = RegressionEvaluator(labelCol="label", predictionCol="prediction")
        metrics = {"mae": evaluator.evaluate(predictions, {evaluator.metricName: "mae"}),
                   "rmse": evaluator.evaluate(predictions, {evaluator.metricName: "rmse"})}
        args.output.mkdir(parents=True, exist_ok=True)
        model.write().overwrite().save(str(args.output / "spark_model"))
        metadata = {
            "model_version": "spark-rf-20260914",
            "algorithm": "Spark MLlib RandomForestRegressor",
            "target": "load_kwh",
            "feature_columns": feature_cols,
            "training_rows": train.count(),
            "validation_rows": test.count(),
            "training_start": train.agg(F.min("date_ts")).collect()[0][0].strftime("%Y-%m-%dT%H:%M:%SZ"),
            "training_end": boundary.strftime("%Y-%m-%dT%H:%M:%SZ"),
            "metrics": metrics,
            "generated_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        }
        (args.output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(metadata, indent=2))
        return 0
    finally:
        spark.stop()


if __name__ == "__main__":
    raise SystemExit(main())
