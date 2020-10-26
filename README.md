# ltcdump

Command line utility for extracting LTC timecodes from an input WAV file.


## Basic usage

user@computer:$ ltcdump input.wav
 *** Timecode range: 18:06:53:05 --> 18:19:04:04

## JSON output 

user@computer:$ ltcdump input.wav -j

{
        "InfoMessages": [
                {"status_code": 0, "string":" Using threshold 4107", "level": 2},
                {"status_code": 0, "string":" Detected FPS=25
", "level": 1},
                {"status_code": 0, "string":" Using threshold 3993", "level": 2},
                {"status_code": 0, "string":" Using threshold 4086", "level": 2},
                {"status_code": 0, "string":" Using threshold 4174", "level": 2},
                {"status_code": 0, "string":" Looking for sync word 00001010000001100000000000000001000010000000001111111111110110100000000000001100", "level": 2},
                {"status_code": 0, "string":" Looking for sync word 00010100000011000000000000000010000100000000011111111111101101000000000000011000", "level": 2},
                {"status_code": 0, "string":" Looking for sync word 00101000000110000000000000000100001000000000111111111111011010000000000000110000", "level": 2},
                {"status_code": 0, "string":" Looking for sync word 01010000001100000000000000001000010000000001111111111110110100000000000001100000", "level": 2},
                {"status_code": 0, "string":" Looking for sync word 10100000011000000000000000010000100000000011111111111101101000000000000011000000", "level": 2},
                {"status_code": 0, "string":" Using threshold 4240", "level": 2}
        ],
        "ErrorMessages": [
        ],
        "TimecodeRanges": [
                ["18:06:53:05", "18:06:53:05"]
        ],
        "ResultCode": 200,
        "ErrorMsg": "",
        "DiscardedBitsAtStart": 0,
        "Start": "00:00:00:00",
        "End": "00:00:00:00"
}
