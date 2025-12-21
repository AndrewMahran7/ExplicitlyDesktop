/*
  ==============================================================================

    LyricsAlignment.h
    Created: 10 Dec 2024
    Author: Explicitly Audio Systems

    Lyrics alignment for improved transcription accuracy.
    Fetches lyrics from lyrics.ovh API and aligns with Whisper timestamps.

  ==============================================================================
*/

#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

/**
    Word segment with timing information from Whisper.
*/
struct WordSegment
{
    std::string word;
    double start;       // Start time in seconds
    double end;         // End time in seconds
    double confidence;  // 0.0-1.0
    
    WordSegment(const std::string& w, double s, double e, double c = 1.0)
        : word(w), start(s), end(e), confidence(c) {}
};

/**
    Preprocessed lyrics word for forced alignment.
*/
struct LyricsWord
{
    int index;
    std::string word;           // Normalized text
    std::string soundex;        // Phonetic encoding for fuzzy matching
    bool isOptional;            // For repeated ad-libs
    
    LyricsWord(int idx, const std::string& w, const std::string& sx = "", bool opt = false)
        : index(idx), word(w), soundex(sx), isOptional(opt) {}
};

/**
    Song metadata from lyrics search.
*/
struct SongInfo
{
    std::string artist;
    std::string title;
    std::string lyrics;
    
    SongInfo() = default;
    SongInfo(const std::string& a, const std::string& t, const std::string& l)
        : artist(a), title(t), lyrics(l) {}
};

/**
    Lyrics fetcher and aligner.
    
    Fetches lyrics from lyrics.ovh API and aligns them with Whisper word timestamps
    using sliding window sequence matching with confidence weighting.
*/
class LyricsAlignment
{
public:
    LyricsAlignment() = default;
    ~LyricsAlignment() = default;
    
    /**
        Fetch lyrics from lyrics.ovh API.
        
        @param artist       Artist name
        @param title        Song title
        @return             Song info with lyrics, or empty if failed
    */
    static SongInfo fetchLyrics(const std::string& artist, const std::string& title);
    
    /**
        Fetch lyrics from lyrics.ovh API (internal implementation).
        
        @param artist       Artist name
        @param title        Song title
        @return             Song info with lyrics, or empty if failed
    */
    static SongInfo fetchLyricsFromOvh(const std::string& artist, const std::string& title);
    
    /**
        Initialize alignment with full song lyrics.
        Prepares lyrics for sliding window alignment.
        
        @param lyrics       Full song lyrics text
    */
    void setLyrics(const std::string& lyrics);
    
    /**
        Align a new transcription chunk using forced alignment.
        
        Treats lyrics as ground truth. Whisper is only used to verify position
        and provide timing. This dramatically improves accuracy by constraining
        the problem space.
        
        @param transcribedWords     Word segments from Whisper (2-second chunk)
        @return                     Corrected word segments using lyrics text
    */
    std::vector<WordSegment> alignChunk(const std::vector<WordSegment>& transcribedWords);
    
    /**
        Check if Whisper output is non-lyrical ([Music], silence, etc.)
        
        @param words    Whisper word segments
        @return         true if should skip alignment
    */
    static bool isNonLyricalContent(const std::vector<WordSegment>& words);
    
    /**
        Calculate text similarity ratio (0.0 - 1.0) using Levenshtein distance.
        
        @param text1    First text
        @param text2    Second text
        @return         Similarity ratio (1.0 = identical)
    */
    static float calculateSimilarity(const std::string& text1, const std::string& text2);
    
    /**
        Simple soundex/metaphone encoding for phonetic matching.
        
        @param word     Word to encode
        @return         Phonetic code
    */
    static std::string soundexEncode(const std::string& word);
    
    /**
        Reset alignment state (for new song or restart).
    */
    void reset();
    
    /**
        Get current position in lyrics (for debugging).
    */
    int getCurrentPosition() const { return currentPosition; }
    
    /**
        Get total number of words in loaded lyrics.
    */
    int getTotalWords() const { return (int)preprocessedLyrics.size(); }
    
    /**
        Check if alignment is locked to sequence.
    */
    bool isLocked() const { return locked; }
    
    /**
        Check if alignment is ready (lyrics loaded and parsed).
    */
    bool isReady() const { return initialized && !preprocessedLyrics.empty(); }
    
    /**
        Align lyrics with transcribed word segments (legacy full-song alignment).
        
        Uses sequence matching to correct misheard words while preserving
        original Whisper timestamps.
        
        @param transcribedWords     Word segments from Whisper
        @param lyrics               User-provided or fetched lyrics
        @return                     Corrected word segments with lyrics text
    */
    static std::vector<WordSegment> alignLyricsToTranscription(
        const std::vector<WordSegment>& transcribedWords,
        const std::string& lyrics
    );
    
    /**
        Normalize text for comparison (lowercase, remove punctuation).
        
        @param text     Input text
        @return         Normalized text
    */
    static std::string normalizeText(const std::string& text);
    
    /**
        Split text into individual words.
        
        @param text     Input text
        @return         Vector of words
    */
    static std::vector<std::string> splitIntoWords(const std::string& text);

public:
    /**
        Predict next words from lyrics when Whisper returns empty.
        Uses currentPosition to estimate which words should be sung.
        
        @param duration     Duration of chunk in seconds
        @return             Predicted word segments with estimated timestamps
    */
    std::vector<WordSegment> predictNextWords(double duration);

private:
    // Forced alignment state
    std::vector<LyricsWord> preprocessedLyrics;  // Structured lyrics with phonemes
    int currentPosition = 0;                      // Current word index in lyrics
    bool locked = false;                          // Are we confident in sequence?
    int consecutiveMatches = 0;                   // Track match confidence
    bool initialized = false;
    
    // Configuration thresholds
    const float TEXT_MATCH_THRESHOLD = 0.20f;     // 20% text similarity for match
    const float PHONEME_MATCH_THRESHOLD = 0.75f;  // 75% soundex similarity
    const float LOCK_THRESHOLD = 0.80f;           // 80% to lock sequence
    const float CONFIDENCE_GATE = 0.50f;          // Below this, snap to expected
    const int LOCK_REQUIRED_MATCHES = 2;          // Consecutive matches to lock
    const int SEARCH_WINDOW = 50;                 // Words to search when unlocked
    
    /**
        Verify if Whisper token matches expected lyrics word.
        Uses text matching, phoneme matching, and confidence gating.
        
        @param whisperWord      Whisper token
        @param expectedWord     Expected lyrics word
        @param outMethod        Output: which method matched
        @return                 Match score 0.0-1.0
    */
    float verifyWord(
        const WordSegment& whisperWord,
        const LyricsWord& expectedWord,
        std::string& outMethod
    );
    
    /**
        Find best starting position in lyrics for Whisper chunk.
        Used for initial lock or when sequence is lost.
        
        @param transcribedWords     Whisper words
        @param searchStart          Start of search range
        @param searchEnd            End of search range
        @param outScore             Output: match score
        @return                     Best position index, or -1
    */
    int findBestStartPosition(
        const std::vector<WordSegment>& transcribedWords,
        int searchStart,
        int searchEnd,
        float& outScore
    );
    
    /**
        Map Whisper timestamps to lyrics words.
        Distributes time proportionally across expected words.
        
        @param lyricsStart      Starting index in preprocessedLyrics
        @param lyricsCount      Number of lyrics words
        @param transcribed      Whisper words (for timing)
        @return                 Word segments with lyrics text
    */
    std::vector<WordSegment> mapTimestamps(
        int lyricsStart,
        int lyricsCount,
        const std::vector<WordSegment>& transcribed
    );
    
    struct AlignmentResult {
        int startPosition;                 // Where in lyrics this chunk starts
        std::vector<WordSegment> correctedWords;
        float avgConfidence;
        int editDistance;
    };
    
    /**
        Find best alignment in lyrics window.
        
        @param transcribed      Transcribed words to align
        @param searchStart      Start of search window in lyrics
        @param searchEnd        End of search window in lyrics
        @return                 Best alignment result
    */
    AlignmentResult findBestAlignment(
        const std::vector<WordSegment>& transcribed,
        int searchStart,
        int searchEnd
    );
    
    /**
        Calculate confidence-weighted edit distance.
        
        @param transcribed      Transcribed words with confidence scores
        @param lyricsSegment    Segment of lyrics words
        @return                 Weighted edit distance
    */
    float calculateWeightedEditDistance(
        const std::vector<WordSegment>& transcribed,
        const std::vector<std::string>& lyricsSegment
    );
    
    /**
        Calculate edit distance matrix for sequence alignment.
        
        @param seq1     First sequence
        @param seq2     Second sequence
        @return         2D edit distance matrix
    */
    static std::vector<std::vector<int>> calculateEditDistance(
        const std::vector<std::string>& seq1,
        const std::vector<std::string>& seq2
    );
    
    /**
        Backtrack through edit distance matrix to find alignment operations.
        
        @param matrix               Edit distance matrix
        @param seq1                 First sequence
        @param seq2                 Second sequence
        @param transcribedWords     Original word segments with timing
        @return                     Aligned word segments with corrected text
    */
    static std::vector<WordSegment> backtrackAlignment(
        const std::vector<std::vector<int>>& matrix,
        const std::vector<std::string>& seq1,
        const std::vector<std::string>& seq2,
        const std::vector<WordSegment>& transcribedWords
    );
    
    /**
        Remove HTML tags from string (utility function).
        
        @param html         HTML string
        @return             Plain text
    */
    static std::string removeHtmlTags(const std::string& html);
    
    /**
        URL-encode a string (utility function).
        
        @param value        String to encode
        @return             URL-encoded string
    */
    static std::string urlEncode(const std::string& value);
};
