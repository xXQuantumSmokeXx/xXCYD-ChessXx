#include "engine.h"
#include <Arduino.h>
#include <string.h>

// ── Material values (centipawns) ─────────────────────────────────────────
static const int MAT_VAL[] = {0, 100, 320, 330, 500, 900, 20000};

// ── Piece-square tables (from white's perspective, rank 0 = bottom) ─────
// Center control, piece activity — scaled so material dominates

// Pawn: advance toward promotion, control center
static const int8_t PST_PAWN[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10,-10,-10, 10, 10,  5,
     5, -5,-10,  0,  0,-10, -5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5,  5, 10, 25, 25, 10,  5,  5,
    10, 10, 20, 30, 30, 20, 10, 10,
    20, 20, 30, 40, 40, 30, 20, 20,
     0,  0,  0,  0,  0,  0,  0,  0,
};

// Knight: center squares best, edges worst
static const int8_t PST_KNIGHT[64] = {
   -20,-10,-10,-10,-10,-10,-10,-20,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -10,  0, 10, 10, 10, 10,  0,-10,
   -10,  0, 10, 15, 15, 10,  0,-10,
   -10,  0, 10, 15, 15, 10,  0,-10,
   -10,  0, 10, 10, 10, 10,  0,-10,
   -10,  0,  0,  5,  5,  0,  0,-10,
   -20,-10,-10,-10,-10,-10,-10,-20,
};

// Bishop: prefer open diagonals, center
static const int8_t PST_BISHOP[64] = {
   -10,-10,-10,-10,-10,-10,-10,-10,
   -10,  5,  0,  0,  0,  0,  5,-10,
   -10,  0, 10, 10, 10, 10,  0,-10,
   -10,  0, 10, 15, 15, 10,  0,-10,
   -10,  0,  5, 10, 10,  5,  0,-10,
   -10,  5,  5,  5,  5,  5,  5,-10,
   -10, 10,  0,  0,  0,  0, 10,-10,
   -10,-10,-20,-10,-10,-20,-10,-10,
};

// Rook: prefer open files, 7th rank
static const int8_t PST_ROOK[64] = {
     0,  0,  0,  5,  5,  0,  0,  0,
    10, 10, 10, 10, 10, 10, 10, 10,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     5, 10, 10, 10, 10, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0,
};

// Queen: slight center preference
static const int8_t PST_QUEEN[64] = {
    -5, -5, -5, -5, -5, -5, -5, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  5,  5,  5,  5,  0, -5,
    -5,  0,  5, 10, 10,  5,  0, -5,
    -5,  0,  5, 10, 10,  5,  0, -5,
    -5,  0,  5,  5,  5,  5,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5, -5, -5, -5, -5, -5, -5, -5,
};

// King: safety in corners early, centralize in endgame
static const int8_t PST_KING_EARLY[64] = {
    10, 20, 10,  0,  0, 10, 20, 10,
    10, 10,  0,  0,  0,  0, 10, 10,
   -10,-10,-10,-10,-10,-10,-10,-10,
   -20,-20,-20,-20,-20,-20,-20,-20,
   -30,-30,-30,-30,-30,-30,-30,-30,
   -40,-40,-40,-40,-40,-40,-40,-40,
   -40,-40,-40,-40,-40,-40,-40,-40,
   -40,-40,-40,-40,-40,-40,-40,-40,
};

static const int8_t PST_KING_END[64] = {
   -10, -5, -5, -5, -5, -5, -5,-10,
    -5,  0,  5,  5,  5,  5,  0, -5,
    -5,  5, 10, 10, 10, 10,  5, -5,
    -5,  5, 10, 15, 15, 10,  5, -5,
    -5,  5, 10, 15, 15, 10,  5, -5,
    -5,  5, 10, 10, 10, 10,  5, -5,
    -5,  0,  5,  5,  5,  5,  0, -5,
   -10, -5, -5, -5, -5, -5, -5,-10,
};

static const int8_t *PST_TABLES[] = {
    nullptr, PST_PAWN, PST_KNIGHT, PST_BISHOP, PST_ROOK, PST_QUEEN, nullptr
};

// ── Game phase detection ─────────────────────────────────────────────────
// Returns a game-phase score: 0 = endgame, 24 = opening
static int gamePhase(const BoardState &s) {
    int phase = 0;
    for (int sq = 0; sq < 64; sq++) {
        int8_t p = s.squares[sq];
        if (p == 0) continue;
        int8_t t = Board::pieceType(p);
        if (t == KNIGHT || t == BISHOP) phase += 1;
        else if (t == ROOK) phase += 2;
        else if (t == QUEEN) phase += 4;
    }
    return phase < 12 ? 0 : (phase > 24 ? 24 : phase);
}

// ── Square table value ───────────────────────────────────────────────────
static int pstValue(int8_t piece, int sq, int phase) {
    int8_t pt = Board::pieceType(piece);
    if (pt == PAWN) {
        int idx = Board::isWhite(piece) ? sq : (63 - sq);
        return PST_PAWN[idx];
    }
    if (pt == KING) {
        int idx = Board::isWhite(piece) ? sq : (63 - sq);
        int early = PST_KING_EARLY[idx];
        int end = PST_KING_END[idx];
        return (early * phase + end * (24 - phase)) / 24;
    }
    const int8_t *tbl = PST_TABLES[pt];
    if (!tbl) return 0;
    int idx = Board::isWhite(piece) ? sq : (63 - sq);
    return tbl[idx];
}

// ── Static evaluation ────────────────────────────────────────────────────
// Positive = white advantage (centipawns)
int engineEvaluate(const Board &board) {
    const BoardState &s = board.state;
    int score = 0;
    int phase = gamePhase(s);

    for (int sq = 0; sq < 64; sq++) {
        int8_t p = s.squares[sq];
        if (p == 0) continue;

        int8_t pt = Board::pieceType(p);
        int mat = MAT_VAL[pt];
        int pst = pstValue(p, sq, phase);

        if (Board::isWhite(p)) {
            score += mat + pst;
        } else {
            score -= mat + pst;
        }
    }

    // Mobility bonus (small — encourages active positions)
    Move temp[64];
    int wMoves = 0, bMoves = 0;
    // Approximate mobility by counting pseudo-legal moves for each side
    // (We'd need to temporarily swap sides — just estimate via piece count)
    // Skip for simplicity — material + PST is sufficient for club-level play.

    return score;
}

// ── Move ordering score (higher = try first) ─────────────────────────────
static int moveScore(const Move &m) {
    int s = 0;
    // Captures: MVV-LVA (most valuable victim - least valuable attacker)
    if (m.captured != 0) {
        s += 1000 + MAT_VAL[Board::pieceType(m.captured)] * 10;
    }
    // Promotions are good
    if (m.promo == QUEEN) s += 900;
    if (m.promo == KNIGHT) s += 300;
    // Center squares are better destinations
    int f = SQ_FILE(m.to), r = SQ_RANK(m.to);
    int distFromCenter = (f > 3 ? 7 - f : f) + (r > 3 ? 7 - r : r);
    s += (6 - distFromCenter);
    return s;
}

// ── Alpha-beta search ────────────────────────────────────────────────────
// Node counter: yield to watchdog every N nodes (prevents reset on long searches)
static int s_searchNodes = 0;
#define YIELD_EVERY_NODES  200

// Static buffers to avoid stack pressure (crash culprit)
static Move  s_moves[256];
static int   s_scores[256];

static int alphaBeta(Board &board, int depth, int alpha, int beta) {
    // Yield periodically to keep the watchdog happy
    if (++s_searchNodes >= YIELD_EVERY_NODES) {
        s_searchNodes = 0;
        yield();
    }

    // Check termination
    int result = board.getResult();
    if (result == RESULT_WHITE_WIN) return INF - (ENGINE_MAX_DEPTH - depth);
    if (result == RESULT_BLACK_WIN) return -INF + (ENGINE_MAX_DEPTH - depth);
    if (result == RESULT_DRAW) return 0;

    if (depth == 0) {
        return engineEvaluate(board);
    }

    // Generate moves with ordering (use static buffers)
    int n = board.genMoves(s_moves, 256);

    // Score moves for ordering
    for (int i = 0; i < n; i++) {
        s_scores[i] = moveScore(s_moves[i]);
    }
    // Simple insertion sort by score (descending)
    for (int i = 1; i < n; i++) {
        Move tm = s_moves[i];
        int ts = s_scores[i];
        int j = i - 1;
        while (j >= 0 && s_scores[j] < ts) {
            s_moves[j + 1] = s_moves[j];
            s_scores[j + 1] = s_scores[j];
            j--;
        }
        s_moves[j + 1] = tm;
        s_scores[j + 1] = ts;
    }

    if (board.state.sideToMove > 0) {
        // Maximizing (white)
        int maxScore = -INF;
        for (int i = 0; i < n; i++) {
            BoardState prev = board.state;
            if (board.makeMove(s_moves[i])) {
                int score = alphaBeta(board, depth - 1, alpha, beta);
                if (score > maxScore) maxScore = score;
                if (score > alpha) alpha = score;
                if (alpha >= beta) {
                    // Restore state before pruning
                    memcpy(&board.state, &prev, sizeof(BoardState));
                    break;
                }
            }
            memcpy(&board.state, &prev, sizeof(BoardState));
        }
        return (n == 0) ? -(INF - (ENGINE_MAX_DEPTH - depth)) : maxScore;
    } else {
        // Minimizing (black)
        int minScore = INF;
        for (int i = 0; i < n; i++) {
            BoardState prev = board.state;
            if (board.makeMove(s_moves[i])) {
                int score = alphaBeta(board, depth - 1, alpha, beta);
                if (score < minScore) minScore = score;
                if (score < beta) beta = score;
                if (alpha >= beta) {
                    memcpy(&board.state, &prev, sizeof(BoardState));
                    break;
                }
            }
            memcpy(&board.state, &prev, sizeof(BoardState));
        }
        return (n == 0) ? (INF - (ENGINE_MAX_DEPTH - depth)) : minScore;
    }
}

// ── Root search — find best move ─────────────────────────────────────────
bool engineThink(const Board &board, Move &bestMove) {
    // LOCAL arrays — recursion uses static s_moves, root uses its own copy
    Move  rootMoves[256];
    int   rootScores[256];

    Board b = board;
    int n = b.genMoves(rootMoves, 256);  // fill LOCAL array
    Serial.print("ENGINE side="); Serial.print(b.state.sideToMove);
    Serial.print(" n="); Serial.print(n);
    if (n>0) { Serial.print(" 1st: "); Serial.print((char)('a'+SQ_FILE(rootMoves[0].from))); Serial.print((char)('1'+SQ_RANK(rootMoves[0].from))); Serial.print("->"); Serial.print((char)('a'+SQ_FILE(rootMoves[0].to))); Serial.print((char)('1'+SQ_RANK(rootMoves[0].to))); Serial.print(" p="); Serial.println((int)b.state.squares[rootMoves[0].from]); }
    Serial.flush();
    if (n == 0) return false;

    s_searchNodes = 0;

    // Score and sort
    for (int i = 0; i < n; i++) rootScores[i] = moveScore(rootMoves[i]);
    for (int i = 1; i < n; i++) {
        Move tm = rootMoves[i]; int ts = rootScores[i]; int j = i - 1;
        while (j >= 0 && rootScores[j] < ts) {
            rootMoves[j+1]=rootMoves[j]; rootScores[j+1]=rootScores[j]; j--;
        }
        rootMoves[j+1]=tm; rootScores[j+1]=ts;
    }

    int bestScore = (board.state.sideToMove > 0) ? -INF : INF;
    bool found = false;

    for (int i = 0; i < n; i++) {
        Move cur = rootMoves[i];  // local — cannot be corrupted by recursion
        BoardState prev = b.state;
        if (!b.makeMove(cur)) {
            memcpy(&b.state, &prev, sizeof(BoardState));
            continue;
        }

        int score = alphaBeta(b, ENGINE_MAX_DEPTH - 1, -INF, INF);
        memcpy(&b.state, &prev, sizeof(BoardState));

        if (board.state.sideToMove > 0) {
            if (!found || score > bestScore) { bestScore = score; bestMove = cur; found = true; }
        } else {
            if (!found || score < bestScore) { bestScore = score; bestMove = cur; found = true; }
        }
    }

    return found;
}
