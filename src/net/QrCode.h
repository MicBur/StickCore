// ---------------------------------------------------------------------------
//  StickCore  –  QrCode.h
//
//  Minimal, self-contained QR Code generator (no external deps).
//  Scope: byte mode, error-correction level L, versions 1–4 (auto-selected).
//  That comfortably encodes any local URL such as
//  "http://192.168.1.42:8080/". Output is a square boolean matrix
//  (true = dark module).
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>

namespace stick {

class QrCode {
public:
    /// Encode 'text' (UTF-8 / ASCII bytes). Returns an NxN matrix of bool.
    /// On failure (text too long for v1–4) returns an empty matrix.
    static std::vector<std::vector<bool>> encode(const std::string& text);

    static int size(const std::vector<std::vector<bool>>& m) { return int(m.size()); }
};

} // namespace stick
