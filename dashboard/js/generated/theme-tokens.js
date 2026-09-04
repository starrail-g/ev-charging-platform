export const themeTokens = {
  "meta": {
    "name": "grid-shift",
    "version": 1
  },
  "themes": {
    "day": {
      "background": "#EDF0EE",
      "surface": "#F7F8F7",
      "text": "#18201D",
      "mutedText": "#5F7068",
      "decorativeStructure": "#CBD2CE",
      "topologyLine": "#5F7068",
      "focusBlue": "#0E6E8C",
      "states": {
        "idle": "#2A7442",
        "reserved": "#8A5A00",
        "charging": "#0E6E8C",
        "fault": "#A94B38",
        "offline": "#5B666B",
        "unknown": "#4E5A55"
      }
    },
    "night": {
      "background": "#080D10",
      "surface": "#0F1E26",
      "text": "#E9F5ED",
      "mutedText": "#A6B0B4",
      "decorativeStructure": "#2A4554",
      "topologyLine": "#6B9184",
      "focusBlue": "#4DD7FF",
      "deepBlue": "#0A3552",
      "states": {
        "idle": "#B7F36A",
        "reserved": "#F5C451",
        "charging": "#4DD7FF",
        "fault": "#FF806D",
        "offline": "#A6B0B4",
        "unknown": "#93A19B"
      }
    }
  },
  "shape": {
    "controlRadius": 6,
    "panelRadius": 8,
    "borderWidth": 1
  },
  "motion": {
    "micro": 150,
    "panel": 200,
    "chargingPulse": 2600,
    "faultPulse": 2800,
    "aurora": 11000
  }
};
