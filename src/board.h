#pragma once
#include <cstdint>

// ── Piece constants ──────────────────────────────────────────────────────
// Positive = white, negative = black, 0 = empty
#define EMPTY     0
#define PAWN      1
#define KNIGHT    2
#define BISHOP    3
#define ROOK      4
#define QUEEN     5
#define KING      6

// ── Square helpers ───────────────────────────────────────────────────────
#define SQ(file, rank)  ((rank) * 8 + (file))
#define SQ_FILE(sq)     ((sq) & 7)
#define SQ_RANK(sq)     ((sq) >> 3)
#define SQ_VALID(sq)    ((sq) >= 0 && (sq) < 64)

// ── Castling flags ───────────────────────────────────────────────────────
#define CASTLE_WK  1
#define CASTLE_WQ  2
#define CASTLE_BK  4
#define CASTLE_BQ  8

// ── Move structure ───────────────────────────────────────────────────────
struct Move {
    int8_t  from;       // 0-63
    int8_t  to;         // 0-63
    int8_t  promo;      // 0=none, or PIECE constant (always positive — color from sideToMove)
    int8_t  captured;   // piece that was captured (for undo / display)

    Move() : from(0), to(0), promo(0), captured(0) {}
    Move(int8_t f, int8_t t, int8_t p = 0, int8_t c = 0)
        : from(f), to(t), promo(p), captured(c) {}
};

// ── Board state ──────────────────────────────────────────────────────────
struct BoardState {
    int8_t  squares[64];
    int8_t  sideToMove;     // 1=white, -1=black
    uint8_t castling;       // bit flags (CASTLE_WK | CASTLE_WQ | CASTLE_BK | CASTLE_BQ)
    int8_t  epSquare;       // en passant target square index, -1 if none
    uint8_t halfMoveClock;
    uint8_t fullMoveNumber;
};

// ── Game result constants ────────────────────────────────────────────────
#define RESULT_NONE       0
#define RESULT_WHITE_WIN  1
#define RESULT_BLACK_WIN  2
#define RESULT_DRAW       3

// ── Board class ──────────────────────────────────────────────────────────
class Board {
public:
    BoardState state;

    Board();

    // Setup
    void setStartPos();
    void clear();

    // Piece helpers
    static bool isWhite(int8_t p) { return p > 0; }
    static bool isBlack(int8_t p) { return p < 0; }
    static int8_t pieceType(int8_t p) { return p > 0 ? p : -p; }
    static bool isSliding(int8_t p) {
        int8_t t = pieceType(p);
        return t == BISHOP || t == ROOK || t == QUEEN;
    }

    // Square attack check
    bool isAttacked(int sq, int8_t bySide) const;

    // Check detection
    bool inCheck(int8_t side) const;
    bool inCheck() const { return inCheck(state.sideToMove); }

    // Move execution
    bool makeMove(const Move &m);
    void undoMove(const Move &m, const BoardState &prev);

    // Move generation
    int  genMoves(Move *moves, int maxMoves) const;       // all legal moves
    int  genPseudoMoves(Move *moves, int maxMoves) const; // pseudo-legal (for engine)
    bool isLegalMove(const Move &m) const;

    // Game state
    int  getResult() const;          // returns RESULT_* constant
    bool isCheckmate() const;
    bool isStalemate() const;

    // Display helpers
    static char pieceChar(int8_t p); // returns 'P','N','B','R','Q','K' or lowercase for black

private:
    bool isAttackedBySliding(int sq, int8_t bySide) const;
    bool isAttackedByPawn(int sq, int8_t bySide) const;
    bool isAttackedByKnight(int sq, int8_t bySide) const;
    bool isAttackedByKing(int sq, int8_t bySide) const;

    void addPawnMoves(Move *moves, int &count, int maxMoves) const;
    void addKnightMoves(Move *moves, int &count, int maxMoves) const;
    void addSlidingMoves(Move *moves, int &count, int maxMoves) const;
    void addKingMoves(Move *moves, int &count, int maxMoves) const;

    void makeMoveOnBoard(const Move &m);
};
