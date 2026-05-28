#pragma once

// Ported from RMT (F:\github\RASTER-Music-Tracker-PeterDell\src\InstrumentTypes.h)
// MFC stripped: BOOL -> bool. Otherwise identical so RMT's encoder/decoder
// (AtaToInstr, InstrToAta) compiles unchanged.

#define INSTRUMENT_NAME_MAX_LEN  32
#define PARCOUNT                 24
#define ENVELOPE_MAX_COLUMNS     48
#define ENVROWS                  8
#define INSTRSNUM                64
#define NOTE_TABLE_MAX_LEN       32
#define NUMBER_OF_PARAMS         20

// Display-hint flags
#define IF_NOEMPTY     1
#define IF_USED        2
#define IF_FILTER      4
#define IF_BASS16      8
#define IF_PORTAMENTO  16
#define IF_AUDCTL      32

// Instrument parameter indices
#define PAR_TBL_LENGTH       0
#define PAR_TBL_GOTO         1
#define PAR_TBL_SPEED        2
#define PAR_TBL_TYPE         3
#define PAR_TBL_MODE         4

#define PAR_ENV_LENGTH       5
#define PAR_ENV_GOTO         6
#define PAR_VOL_FADEOUT      7
#define PAR_VOL_MIN          8
#define PAR_DELAY            9
#define PAR_VIBRATO         10
#define PAR_FREQ_SHIFT      11

#define PAR_AUDCTL_15KHZ    12
#define PAR_AUDCTL_HPF_CH2  13
#define PAR_AUDCTL_HPF_CH1  14
#define PAR_AUDCTL_JOIN_3_4 15
#define PAR_AUDCTL_JOIN_1_2 16
#define PAR_AUDCTL_179_CH3  17
#define PAR_AUDCTL_179_CH1  18
#define PAR_AUDCTL_POLY9    19

#define INSTRUMENT_TABLE_OF_NOTES  1
#define INSTRUMENT_TABLE_OF_FREQ   2
#define INSTRUMENT_TABLE_MODE_SET  3
#define INSTRUMENT_TABLE_MODE_ADD  4

enum EnvelopeParameter {
    VOLUMER    = 0,
    VOLUMEL    = 1,
    FILTER     = 2,
    COMMAND    = 3,
    DISTORTION = 4,
    PORTAMENTO = 5,
    X          = 6,
    Y          = 7,
};

enum class InstrumentSection : int {
    NONE       = -1,
    NAME       = 0,
    PARAMETERS = 1,
    ENVELOPE   = 2,
    NOTETABLE  = 3,
};

struct TInstrument {
    InstrumentSection activeEditSection;

    char name[INSTRUMENT_NAME_MAX_LEN + 1];
    int  editNameCursorPos;

    int  parameters[PARCOUNT];
    int  editParameterNr;

    int  envelope[ENVELOPE_MAX_COLUMNS][ENVROWS];
    int  editEnvelopeX;
    int  editEnvelopeY;

    int  noteTable[NOTE_TABLE_MAX_LEN];
    int  editNoteTableCursorPos;

    int  octave;
    int  volume;

    int  displayHintFlags;
};
