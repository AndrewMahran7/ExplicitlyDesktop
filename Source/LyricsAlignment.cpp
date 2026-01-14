/*
  ==============================================================================

    LyricsAlignment.cpp
    Created: 10 Dec 2024
    Author: Explicitly Audio Systems

    Implementation of lyrics fetching and alignment.

  ==============================================================================
*/

#include "LyricsAlignment.h"
#include <juce_core/juce_core.h>
#include <iostream>
#include <sstream>
#include <regex>
#include <algorithm>

// Normalize text: lowercase, remove punctuation, trim whitespace
std::string LyricsAlignment::normalizeText(const std::string& text)
{
    std::string result = text;
    
    // Convert to lowercase
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    
    // Remove punctuation (keep only alphanumeric and spaces)
    result.erase(std::remove_if(result.begin(), result.end(), 
        [](char c) { return !std::isalnum(c) && !std::isspace(c); }), result.end());
    
    // Remove extra whitespace
    std::istringstream iss(result);
    std::string word;
    std::vector<std::string> words;
    while (iss >> word)
        words.push_back(word);
    
    result.clear();
    for (size_t i = 0; i < words.size(); ++i)
    {
        if (i > 0) result += " ";
        result += words[i];
    }
    
    return result;
}

// Split text into individual words
std::vector<std::string> LyricsAlignment::splitIntoWords(const std::string& text)
{
    std::string normalized = normalizeText(text);
    std::istringstream iss(normalized);
    std::string word;
    std::vector<std::string> words;
    
    while (iss >> word)
        words.push_back(word);
    
    return words;
}

// Fetch lyrics from lyrics.ovh API
SongInfo LyricsAlignment::fetchLyrics(const std::string& artist, const std::string& title)
{
    std::cout << "[Lyrics] Fetching lyrics for: " << artist << " - " << title << std::endl;
    
    SongInfo info = fetchLyricsFromOvh(artist, title);
    
    if (!info.lyrics.empty())
    {
        std::cout << "[Lyrics] ✓ Successfully fetched from lyrics.ovh" << std::endl;
    }
    else
    {
        std::cout << "[Lyrics] ✗ Failed to fetch lyrics" << std::endl;
    }
    
    return info;
}

// Fetch lyrics from lyrics.ovh API
SongInfo LyricsAlignment::fetchLyricsFromOvh(const std::string& artist, const std::string& title)
{
    std::cout << "[LyricsOVH] Fetching lyrics for: " << artist << " - " << title << std::endl;
    
    // URL encode artist and title
    juce::String encodedArtist = juce::URL::addEscapeChars(artist, false);
    juce::String encodedTitle = juce::URL::addEscapeChars(title, false);
    
    // Build API URL
    juce::String apiUrl = "https://api.lyrics.ovh/v1/" + encodedArtist + "/" + encodedTitle;
    juce::URL url(apiUrl);
    
    std::cout << "[Lyrics] Request URL: " << apiUrl << std::endl;
    
    // Make HTTP request with timeout
    juce::URL::InputStreamOptions options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
        .withConnectionTimeoutMs(10000)
        .withNumRedirectsToFollow(5);
    
    std::unique_ptr<juce::InputStream> stream = url.createInputStream(options);
    
    if (!stream)
    {
        std::cout << "[Lyrics] Failed to connect to API" << std::endl;
        return SongInfo();
    }
    
    // Read response
    juce::String response = stream->readEntireStreamAsString();
    
    if (response.isEmpty())
    {
        std::cout << "[Lyrics] Empty response from API" << std::endl;
        return SongInfo();
    }
    
    // Parse JSON response
    juce::var json = juce::JSON::parse(response);
    
    if (!json.isObject())
    {
        std::cout << "[Lyrics] Invalid JSON response" << std::endl;
        return SongInfo();
    }
    
    // Extract lyrics
    juce::var lyricsVar = json["lyrics"];
    
    if (lyricsVar.isVoid())
    {
        std::cout << "[Lyrics] No lyrics found in response" << std::endl;
        return SongInfo();
    }
    
    std::string lyrics = lyricsVar.toString().toStdString();
    
    if (lyrics.empty())
    {
        std::cout << "[Lyrics] Lyrics field is empty" << std::endl;
        return SongInfo();
    }
    
    std::cout << "[Lyrics] Successfully fetched " << lyrics.length() << " characters" << std::endl;
    
    return SongInfo(artist, title, lyrics);
}

// URL-encode a string
std::string LyricsAlignment::urlEncode(const std::string& value)
{
    std::string encoded;
    
    for (char c : value)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            encoded += c;
        }
        else if (c == ' ')
        {
            encoded += '+';
        }
        else
        {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned char>(c));
            encoded += hex;
        }
    }
    
    return encoded;
}

// Calculate edit distance matrix using dynamic programming
std::vector<std::vector<int>> LyricsAlignment::calculateEditDistance(
    const std::vector<std::string>& seq1,
    const std::vector<std::string>& seq2)
{
    int m = seq1.size();
    int n = seq2.size();
    
    // Create matrix (m+1) x (n+1)
    std::vector<std::vector<int>> matrix(m + 1, std::vector<int>(n + 1, 0));
    
    // Initialize first row and column
    for (int i = 0; i <= m; ++i)
        matrix[i][0] = i;
    for (int j = 0; j <= n; ++j)
        matrix[0][j] = j;
    
    // Fill matrix using dynamic programming
    for (int i = 1; i <= m; ++i)
    {
        for (int j = 1; j <= n; ++j)
        {
            if (seq1[i-1] == seq2[j-1])
            {
                // Words match - no cost
                matrix[i][j] = matrix[i-1][j-1];
            }
            else
            {
                // Words differ - take minimum of insert, delete, replace
                matrix[i][j] = 1 + std::min({
                    matrix[i-1][j],     // Delete
                    matrix[i][j-1],     // Insert
                    matrix[i-1][j-1]    // Replace
                });
            }
        }
    }
    
    return matrix;
}

// Backtrack through edit distance matrix to create aligned segments
std::vector<WordSegment> LyricsAlignment::backtrackAlignment(
    const std::vector<std::vector<int>>& matrix,
    const std::vector<std::string>& transcribedWords,
    const std::vector<std::string>& lyricsWords,
    const std::vector<WordSegment>& originalSegments)
{
    std::vector<WordSegment> correctedSegments;
    
    int i = transcribedWords.size();
    int j = lyricsWords.size();
    
    // Backtrack from bottom-right to top-left
    std::vector<std::pair<int, int>> alignments;  // (transcribed_idx, lyrics_idx)
    
    while (i > 0 || j > 0)
    {
        if (i > 0 && j > 0 && transcribedWords[i-1] == lyricsWords[j-1])
        {
            // Match - use this alignment
            alignments.push_back({i-1, j-1});
            i--; j--;
        }
        else if (i > 0 && j > 0 && matrix[i][j] == matrix[i-1][j-1] + 1)
        {
            // Replace - align these positions
            alignments.push_back({i-1, j-1});
            i--; j--;
        }
        else if (j > 0 && matrix[i][j] == matrix[i][j-1] + 1)
        {
            // Insert from lyrics - create estimated timing
            alignments.push_back({-1, j-1});
            j--;
        }
        else if (i > 0)
        {
            // Delete from transcription - skip this word
            i--;
        }
    }
    
    // Reverse alignments (we backtracked from end to start)
    std::reverse(alignments.begin(), alignments.end());
    
    // Create corrected segments
    for (const auto& [trans_idx, lyrics_idx] : alignments)
    {
        if (trans_idx >= 0 && lyrics_idx >= 0)
        {
            // Use timing from transcribed word, text from lyrics
            const WordSegment& original = originalSegments[trans_idx];
            correctedSegments.emplace_back(
                lyricsWords[lyrics_idx],
                original.start,
                original.end,
                original.confidence * 0.95  // Slightly lower confidence for corrections
            );
        }
        else if (lyrics_idx >= 0)
        {
            // Lyrics word not in transcription - estimate timing
            double estimatedStart = 0.0;
            double estimatedEnd = 0.3;  // Average word duration
            
            if (!correctedSegments.empty())
            {
                estimatedStart = correctedSegments.back().end;
                estimatedEnd = estimatedStart + 0.3;
            }
            
            correctedSegments.emplace_back(
                lyricsWords[lyrics_idx],
                estimatedStart,
                estimatedEnd,
                0.5  // Low confidence for estimated timing
            );
        }
    }
    
    return correctedSegments;
}

// Main alignment function
std::vector<WordSegment> LyricsAlignment::alignLyricsToTranscription(
    const std::vector<WordSegment>& transcribedWords,
    const std::string& lyrics)
{
    if (lyrics.empty() || transcribedWords.empty())
    {
        std::cout << "[Lyrics Alignment] Empty input - returning original transcription" << std::endl;
        return transcribedWords;
    }
    
    // Extract normalized words from transcription
    std::vector<std::string> transcribedText;
    for (const auto& seg : transcribedWords)
        transcribedText.push_back(normalizeText(seg.word));
    
    // Split lyrics into normalized words
    std::vector<std::string> lyricsWords = splitIntoWords(lyrics);
    
    std::cout << "[Lyrics Alignment] Transcribed: " << transcribedText.size() << " words" << std::endl;
    std::cout << "[Lyrics Alignment] Lyrics: " << lyricsWords.size() << " words" << std::endl;
    
    if (lyricsWords.empty())
    {
        std::cout << "[Lyrics Alignment] No valid words in lyrics" << std::endl;
        return transcribedWords;
    }
    
    // Calculate edit distance matrix
    std::vector<std::vector<int>> matrix = calculateEditDistance(transcribedText, lyricsWords);
    
    // Backtrack to create aligned segments
    std::vector<WordSegment> correctedSegments = backtrackAlignment(
        matrix, transcribedText, lyricsWords, transcribedWords
    );
    
    std::cout << "[Lyrics Alignment] Corrected: " << correctedSegments.size() << " words" << std::endl;
    
    // Count corrections
    int corrections = 0;
    for (size_t i = 0; i < std::min(correctedSegments.size(), transcribedWords.size()); ++i)
    {
        if (normalizeText(correctedSegments[i].word) != normalizeText(transcribedWords[i].word))
            corrections++;
    }
    
    std::cout << "[Lyrics Alignment] Corrections made: " << corrections << std::endl;
    
    return correctedSegments;
}

// ========== FORCED ALIGNMENT IMPLEMENTATION ==========

// Simple soundex implementation for phonetic matching
std::string LyricsAlignment::soundexEncode(const std::string& word)
{
    if (word.empty()) return "";
    
    std::string normalized = normalizeText(word);
    if (normalized.empty()) return "";
    
    // Simplified soundex algorithm
    std::string code;
    code += std::toupper(normalized[0]);  // Keep first letter
    
    // Encode remaining letters
    for (size_t i = 1; i < normalized.length() && code.length() < 4; ++i)
    {
        char c = normalized[i];
        char digit = '0';
        
        // Consonant groups
        if (c == 'b' || c == 'f' || c == 'p' || c == 'v') digit = '1';
        else if (c == 'c' || c == 'g' || c == 'j' || c == 'k' || c == 'q' || c == 's' || c == 'x' || c == 'z') digit = '2';
        else if (c == 'd' || c == 't') digit = '3';
        else if (c == 'l') digit = '4';
        else if (c == 'm' || c == 'n') digit = '5';
        else if (c == 'r') digit = '6';
        
        // Skip vowels and repeats
        if (digit != '0' && (code.empty() || code.back() != digit))
            code += digit;
    }
    
    // Pad with zeros
    while (code.length() < 4)
        code += '0';
    
    return code.substr(0, 4);
}

// Check if Whisper output is non-lyrical
bool LyricsAlignment::isNonLyricalContent(const std::vector<WordSegment>& words)
{
    if (words.empty())
        return true;
    
    // Build combined text
    std::string combined;
    for (const auto& word : words)
        combined += normalizeText(word.word) + " ";
    
    combined = normalizeText(combined);
    
    // Non-lyrical patterns
    if (combined.empty() || combined.length() < 2)
        return true;
    
    if (combined.find("music") != std::string::npos ||
        combined.find("applause") != std::string::npos ||
        combined.find("laughter") != std::string::npos ||
        combined.find("instrumental") != std::string::npos)
    {
        std::cout << "[ForceAlign] Skipping non-lyrical: \"" << combined << "\"" << std::endl;
        return true;
    }
    
    return false;
}

// Calculate Levenshtein-based similarity
float LyricsAlignment::calculateSimilarity(const std::string& text1, const std::string& text2)
{
    std::string s1 = normalizeText(text1);
    std::string s2 = normalizeText(text2);
    
    if (s1.empty() && s2.empty()) return 1.0f;
    if (s1.empty() || s2.empty()) return 0.0f;
    if (s1 == s2) return 1.0f;
    
    int m = s1.length();
    int n = s2.length();
    
    // Use simple character-level edit distance
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));
    
    for (int i = 0; i <= m; ++i) dp[i][0] = i;
    for (int j = 0; j <= n; ++j) dp[0][j] = j;
    
    for (int i = 1; i <= m; ++i)
    {
        for (int j = 1; j <= n; ++j)
        {
            if (s1[i-1] == s2[j-1])
                dp[i][j] = dp[i-1][j-1];
            else
                dp[i][j] = 1 + std::min({dp[i-1][j], dp[i][j-1], dp[i-1][j-1]});
        }
    }
    
    int distance = dp[m][n];
    int maxLen = std::max(m, n);
    
    return 1.0f - (float)distance / maxLen;
}

void LyricsAlignment::setLyrics(const std::string& lyrics)
{
    std::cout << "[ForceAlign] Initializing forced alignment (" << lyrics.length() << " chars)" << std::endl;
    
    preprocessedLyrics.clear();
    currentPosition = 0;
    locked = false;
    consecutiveMatches = 0;
    
    // Tokenize and preprocess
    std::vector<std::string> words = splitIntoWords(lyrics);
    
    for (size_t i = 0; i < words.size(); ++i)
    {
        std::string word = words[i];
        std::string soundex = soundexEncode(word);
        preprocessedLyrics.emplace_back(i, word, soundex, false);
    }
    
    initialized = true;
    
    std::cout << "[ForceAlign] Loaded " << preprocessedLyrics.size() << " words" << std::endl;
    std::cout << "[ForceAlign] Example: \"" << (preprocessedLyrics.empty() ? "" : preprocessedLyrics[0].word) 
              << "\" [" << (preprocessedLyrics.empty() ? "" : preprocessedLyrics[0].soundex) << "]" << std::endl;
}

void LyricsAlignment::reset()
{
    currentPosition = 0;
    locked = false;
    consecutiveMatches = 0;
    initialized = false;
    preprocessedLyrics.clear();
    std::cout << "[ForceAlign] Reset" << std::endl;
}

float LyricsAlignment::calculateWeightedEditDistance(
    const std::vector<WordSegment>& transcribed,
    const std::vector<std::string>& lyricsSegment)
{
    int m = transcribed.size();
    int n = lyricsSegment.size();
    
    if (m == 0) return (float)n;
    if (n == 0) return (float)m;
    
    // Create weighted distance matrix
    std::vector<std::vector<float>> dp(m + 1, std::vector<float>(n + 1, 0.0f));
    
    // Initialize first row and column
    for (int i = 0; i <= m; ++i)
        dp[i][0] = (float)i;
    for (int j = 0; j <= n; ++j)
        dp[0][j] = (float)j;
    
    // Fill matrix with confidence weighting
    for (int i = 1; i <= m; ++i)
    {
        for (int j = 1; j <= n; ++j)
        {
            std::string transWord = normalizeText(transcribed[i-1].word);
            std::string lyricsWord = lyricsSegment[j-1];
            float confidence = transcribed[i-1].confidence;
            
            if (transWord == lyricsWord)
            {
                // Match - no cost
                dp[i][j] = dp[i-1][j-1];
            }
            else
            {
                // Mismatch - use confidence to weight the cost
                // Low confidence words have lower mismatch penalty
                float mismatchCost = 1.0f + confidence;  // Range: 1.0 to 2.0
                
                dp[i][j] = std::min({
                    dp[i-1][j] + 1.0f,              // Delete
                    dp[i][j-1] + 1.0f,              // Insert
                    dp[i-1][j-1] + mismatchCost     // Replace (weighted)
                });
            }
        }
    }
    
    return dp[m][n];
}

LyricsAlignment::AlignmentResult LyricsAlignment::findBestAlignment(
    const std::vector<WordSegment>& transcribed,
    int searchStart,
    int searchEnd)
{
    AlignmentResult best;
    best.editDistance = 99999;
    best.avgConfidence = 0.0f;
    
    int transcribedSize = transcribed.size();
    if (transcribedSize == 0 || searchStart >= searchEnd)
        return best;
    
    // Try different window positions
    for (int startPos = searchStart; startPos < searchEnd; ++startPos)
    {
        // Extract lyrics window of same size as transcribed
        int endPos = std::min(startPos + transcribedSize, (int)preprocessedLyrics.size());
        if (endPos - startPos < transcribedSize / 2)
            break;  // Window too small
        
        std::vector<std::string> lyricsWindow;
        for (int i = startPos; i < endPos; ++i)
            lyricsWindow.push_back(preprocessedLyrics[i].word);
        
        // Calculate weighted edit distance
        float distance = calculateWeightedEditDistance(transcribed, lyricsWindow);
        
        // Normalize by transcription length
        float normalizedDistance = distance / transcribedSize;
        
        // Update best if this is better
        if (normalizedDistance < best.editDistance / transcribedSize)
        {
            best.startPosition = startPos;
            best.editDistance = (int)distance;
            
            // Create aligned segments
            best.correctedWords.clear();
            for (size_t i = 0; i < transcribed.size() && (startPos + i) < preprocessedLyrics.size(); ++i)
            {
                const auto& orig = transcribed[i];
                best.correctedWords.emplace_back(
                    preprocessedLyrics[startPos + i].word,
                    orig.start,
                    orig.end,
                    orig.confidence * 0.95f  // Slightly lower confidence for corrections
                );
            }
            
            // Calculate average confidence
            float confSum = 0.0f;
            for (const auto& seg : transcribed)
                confSum += seg.confidence;
            best.avgConfidence = confSum / transcribed.size();
        }
    }
    
    return best;
}

// Verify word match using text, phonemes, and confidence
float LyricsAlignment::verifyWord(
    const WordSegment& whisperWord,
    const LyricsWord& expectedWord,
    std::string& outMethod)
{
    // Method 1: Text match
    float textSim = calculateSimilarity(whisperWord.word, expectedWord.word);
    if (textSim >= TEXT_MATCH_THRESHOLD)
    {
        outMethod = "text";
        return textSim;
    }
    
    // Method 2: Phoneme (soundex) match
    std::string whisperSoundex = soundexEncode(whisperWord.word);
    float phonemeSim = (whisperSoundex == expectedWord.soundex) ? 1.0f : 0.0f;
    if (phonemeSim >= PHONEME_MATCH_THRESHOLD)
    {
        outMethod = "phoneme";
        return phonemeSim;
    }
    
    // Method 3: Confidence gating - if Whisper unsure, snap to expected
    if (whisperWord.confidence < CONFIDENCE_GATE && textSim >= 0.5f)
    {
        outMethod = "confidence_gate";
        return 0.75f;  // Moderate confidence for gated match
    }
    
    outMethod = "none";
    return textSim;  // Return text similarity even if no match
}

// Find best starting position for Whisper chunk in lyrics
int LyricsAlignment::findBestStartPosition(
    const std::vector<WordSegment>& transcribedWords,
    int searchStart,
    int searchEnd,
    float& outScore)
{
    int bestPosition = -1;
    float bestScore = 0.0f;
    
    // Build transcribed text
    std::string transcribedText;
    for (const auto& word : transcribedWords)
        transcribedText += word.word + " ";
    transcribedText = normalizeText(transcribedText);
    
    // Search through lyrics window
    for (int pos = searchStart; pos < searchEnd && pos < (int)preprocessedLyrics.size(); ++pos)
    {
        // Build lyrics window of same size
        std::string lyricsText;
        int endPos = std::min(pos + (int)transcribedWords.size(), (int)preprocessedLyrics.size());
        
        for (int i = pos; i < endPos; ++i)
            lyricsText += preprocessedLyrics[i].word + " ";
        
        lyricsText = normalizeText(lyricsText);
        
        // Calculate similarity
        float score = calculateSimilarity(transcribedText, lyricsText);
        
        if (score > bestScore)
        {
            bestScore = score;
            bestPosition = pos;
        }
    }
    
    outScore = bestScore;
    return bestPosition;
}

// Map timestamps to lyrics words
std::vector<WordSegment> LyricsAlignment::mapTimestamps(
    int lyricsStart,
    int lyricsCount,
    const std::vector<WordSegment>& transcribed)
{
    std::vector<WordSegment> result;
    
    if (transcribed.empty() || lyricsCount == 0)
        return result;
    
    // Get time span
    double startTime = transcribed.front().start;
    double endTime = transcribed.back().end;
    double totalDuration = endTime - startTime;
    
    // Distribute time proportionally
    double timePerWord = totalDuration / lyricsCount;
    
    // Average confidence from transcription
    double avgConf = 0.0;
    for (const auto& w : transcribed)
        avgConf += w.confidence;
    avgConf /= transcribed.size();
    
    // Create word segments with lyrics text
    for (int i = 0; i < lyricsCount; ++i)
    {
        int lyricsIdx = lyricsStart + i;
        if (lyricsIdx >= (int)preprocessedLyrics.size())
            break;
        
        double wordStart = startTime + (i * timePerWord);
        double wordEnd = wordStart + timePerWord;
        
        result.emplace_back(
            preprocessedLyrics[lyricsIdx].word,
            wordStart,
            wordEnd,
            avgConf * 0.95  // Slightly reduce confidence for aligned words
        );
    }
    
    return result;
}

// Main forced alignment logic
std::vector<WordSegment> LyricsAlignment::alignChunk(
    const std::vector<WordSegment>& transcribedWords,
    double absoluteTime)
{
    // Check if ready
    if (!initialized || preprocessedLyrics.empty())
    {
        std::cout << "[ForceAlign] Not initialized - using raw Whisper" << std::endl;
        return transcribedWords;
    }
    
    if (transcribedWords.empty())
    {
        std::cout << "[ForceAlign] Empty chunk" << std::endl;
        return transcribedWords;
    }
    
    // Step 1: Check for non-lyrical content
    if (isNonLyricalContent(transcribedWords))
    {
        // Don't advance position - probably instrumental
        std::cout << "[ForceAlign] Non-lyrical - position frozen at " << currentPosition << std::endl;
        return transcribedWords;
    }
    
    // Step 2: Use absolute time to estimate position and constrain search
    int estimatedPosition = currentPosition;  // Default to sequential position
    
    if (absoluteTime > 0.0)
    {
        // Estimate position based on elapsed time (average 3.5 words/second for rap)
        const double WORDS_PER_SECOND = 3.5;
        estimatedPosition = (int)(absoluteTime * WORDS_PER_SECOND);
        
        // Detect large jumps (user likely skipped forward/backward)
        int positionDelta = std::abs(estimatedPosition - currentPosition);
        if (positionDelta > 20 && locked)
        {
            std::cout << "[ForceAlign] Large time jump detected (" << positionDelta 
                     << " words) - unlocking sequence" << std::endl;
            locked = false;
            consecutiveMatches = 0;
        }
    }
    
    // Step 3: Determine search range
    int searchStart, searchEnd;
    
    if (!locked || currentPosition == 0)
    {
        // Unlocked: search around estimated position based on time
        const int TIME_BASED_WINDOW = 30;  // ±30 words from time estimate
        searchStart = std::max(0, estimatedPosition - TIME_BASED_WINDOW);
        searchEnd = std::min((int)preprocessedLyrics.size(), estimatedPosition + TIME_BASED_WINDOW);
        std::cout << "[ForceAlign] UNLOCKED - time-based search [" << searchStart << "-" << searchEnd 
                 << "] (time=" << absoluteTime << "s, est_pos=" << estimatedPosition << ")" << std::endl;
    }
    else
    {
        // Locked: narrow search (following sequence)
        searchStart = currentPosition;
        searchEnd = std::min((int)preprocessedLyrics.size(), currentPosition + 10);
        std::cout << "[ForceAlign] LOCKED - continuing from " << currentPosition << std::endl;
    }
    
    // Step 3: Find best match
    float matchScore;
    int matchPosition = findBestStartPosition(transcribedWords, searchStart, searchEnd, matchScore);
    
    if (matchPosition < 0)
    {
        std::cout << "[ForceAlign] No match found - using raw Whisper" << std::endl;
        locked = false;
        consecutiveMatches = 0;
        return transcribedWords;
    }
    
    std::cout << "[ForceAlign] Match at position " << matchPosition 
              << ", score: " << (matchScore * 100.0f) << "%" << std::endl;
    
    // Step 4: Evaluate match confidence
    if (matchScore >= LOCK_THRESHOLD)
    {
        // Strong match - lock or maintain lock
        consecutiveMatches++;
        
        if (consecutiveMatches >= LOCK_REQUIRED_MATCHES)
        {
            if (!locked)
            {
                std::cout << "[ForceAlign] ✓ LOCKED to sequence (consecutive matches: " 
                          << consecutiveMatches << ")" << std::endl;
            }
            locked = true;
        }
        
        // Map timestamps to lyrics
        int wordCount = std::min((int)transcribedWords.size(), 
                                (int)preprocessedLyrics.size() - matchPosition);
        std::vector<WordSegment> aligned = mapTimestamps(matchPosition, wordCount, transcribedWords);
        
        // Advance position
        currentPosition = matchPosition + wordCount;
        
        std::cout << "[ForceAlign] ✓ Using lyrics [" << matchPosition << "-" 
                  << (matchPosition + wordCount) << "], next position: " << currentPosition << std::endl;
        
        return aligned;
    }
    else if (matchScore >= TEXT_MATCH_THRESHOLD)
    {
        // Decent match - use but don't lock
        std::cout << "[ForceAlign] Decent match (not locking)" << std::endl;
        
        locked = false;
        consecutiveMatches = 0;
        
        int wordCount = std::min((int)transcribedWords.size(), 
                                (int)preprocessedLyrics.size() - matchPosition);
        std::vector<WordSegment> aligned = mapTimestamps(matchPosition, wordCount, transcribedWords);
        
        currentPosition = matchPosition + wordCount;
        
        return aligned;
    }
    else
    {
        // Weak match - use raw Whisper
        std::cout << "[ForceAlign] ✗ Match too weak (" << (matchScore * 100.0f) 
                  << "%) - using raw Whisper" << std::endl;
        
        locked = false;
        consecutiveMatches = 0;
        
        return transcribedWords;
    }
}

std::vector<WordSegment> LyricsAlignment::predictNextWords(double duration)
{
    if (!isReady() || currentPosition >= (int)preprocessedLyrics.size())
        return {};
    
    std::vector<WordSegment> predictedWords;
    
    // Estimate words per second (typical rap: ~4-6 wps, singing: ~2-3 wps)
    const double ESTIMATED_WORDS_PER_SECOND = 3.5;
    int numWords = std::min(
        (int)(duration * ESTIMATED_WORDS_PER_SECOND),
        (int)preprocessedLyrics.size() - currentPosition
    );
    
    // Distribute predicted words evenly across the chunk duration
    double wordDuration = duration / numWords;
    
    std::cout << "[LyricsPredict] Predicting " << numWords << " words from position " 
              << currentPosition << " (" << wordDuration << "s per word)" << std::endl;
    
    for (int i = 0; i < numWords; ++i)
    {
        const auto& lyricsWord = preprocessedLyrics[currentPosition + i];
        
        double startTime = i * wordDuration;
        double endTime = startTime + wordDuration;
        
        predictedWords.emplace_back(
            lyricsWord.word,
            startTime,
            endTime,
            0.5f  // Low confidence (prediction, not verified)
        );
    }
    
    // Advance position (will be corrected if prediction was wrong)
    currentPosition += numWords;
    
    return predictedWords;
}

// ========== OLD ALIGNMENT METHODS (KEEP FOR LEGACY) ==========

// alignLyricsToTranscription is now implemented above with forced alignment
