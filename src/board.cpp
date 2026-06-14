#include "board.h"
#include <string.h>

// ── Direction offsets for sliding pieces ─────────────────────────────────
static const int8_t DIR_BISHOP[] = {-9, -7, 7, 9};
static const int8_t DIR_ROOK[]   = {-8, -1, 1, 8};
static const int8_t DIR_QUEEN[]  = {-9, -8, -7, -1, 1, 7, 8, 9};

static const int8_t KNIGHT_MOVES[] = {-17, -15, -10, -6, 6, 10, 15, 17};
static const int8_t KING_MOVES[]   = {-9, -8, -7, -1, 1, 7, 8, 9};

// ── Constructor ──────────────────────────────────────────────────────────
Board::Board() {
    clear();
}

void Board::clear() {
    memset(state.squares, 0, sizeof(state.squares));
    state.sideToMove = 1;
    state.castling = 0;
    state.epSquare = -1;
    state.halfMoveClock = 0;
    state.fullMoveNumber = 1;
}

// ── Starting position ────────────────────────────────────────────────────
void Board::setStartPos() {
    clear();
    // Piece placement
    const int8_t backRank[] = {ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK};
    for (int f = 0; f < 8; f++) {
        state.squares[SQ(f, 0)] =  backRank[f];    // white back rank (rank 0 = a1..h1, bottom)
        state.squares[SQ(f, 1)] =  PAWN;            // white pawns
        state.squares[SQ(f, 6)] = -PAWN;            // black pawns
        state.squares[SQ(f, 7)] = -backRank[f];     // black back rank (rank 7 = top)
    }
    state.sideToMove = 1;
    state.castling = CASTLE_WK | CASTLE_WQ | CASTLE_BK | CASTLE_BQ;
    state.epSquare = -1;
    state.halfMoveClock = 0;
    state.fullMoveNumber = 1;
}

// ── Coordinate validation for sliding ray ────────────────────────────────
// Returns false when a ray steps off the board (preventing file-wrap)
static bool validSlide(int from, int to) {
    int f1 = SQ_FILE(from), r1 = SQ_RANK(from);
    int f2 = SQ_FILE(to),   r2 = SQ_RANK(to);
    int df = f2 - f1, dr = r2 - r1;
    if (df < 0) df = -df;
    if (dr < 0) dr = -dr;
    // Diagonal: |df| == |dr|;  straight: df==0 || dr==0
    if (df == dr || df == 0 || dr == 0) return true;
    return false;
}

// ── Attack detection ─────────────────────────────────────────────────────

bool Board::isAttackedByPawn(int sq, int8_t bySide) const {
    // bySide attacks from ranks above/below: white pawns attack from rank+1 (diagonally down on board)
    // White pawns on rank r attack r+1 (southwest/southeast)
    // Black pawns on rank r attack r-1 (northwest/northeast)
    int f = SQ_FILE(sq), r = SQ_RANK(sq);
    // White pawns move up (+rank), attack rank+1 → pawn is at rank-1 below target
    // Black pawns move down (-rank), attack rank-1 → pawn is at rank+1 above target
    int dir = (bySide > 0) ? 1 : -1;
    int pawnRank = r - dir;  // the rank the attacking pawn would be on
    if (pawnRank < 0 || pawnRank > 7) return false;
    for (int df = -1; df <= 1; df += 2) {
        int pf = f + df;
        if (pf < 0 || pf > 7) continue;
        int8_t p = state.squares[SQ(pf, pawnRank)];
        if (p * bySide > 0 && pieceType(p) == PAWN) return true;
    }
    return false;
}

bool Board::isAttackedByKnight(int sq, int8_t bySide) const {
    int f = SQ_FILE(sq), r = SQ_RANK(sq);
    for (int i = 0; i < 8; i++) {
        int tsq = sq + KNIGHT_MOVES[i];
        if (!SQ_VALID(tsq)) continue;
        int tf = SQ_FILE(tsq), tr = SQ_RANK(tsq);
        int df = tf > f ? tf - f : f - tf;
        int dr = tr > r ? tr - r : r - tr;
        if ((df == 1 && dr == 2) || (df == 2 && dr == 1)) {
            int8_t p = state.squares[tsq];
            if (p * bySide > 0 && pieceType(p) == KNIGHT) return true;
        }
    }
    return false;
}

bool Board::isAttackedBySliding(int sq, int8_t bySide) const {
    int f = SQ_FILE(sq), r = SQ_RANK(sq);
    // Check 8 directions for rooks/queens/bishops
    static const int8_t dirs[] = {-1, 1, -8, 8, -9, -7, 7, 9};
    for (int d = 0; d < 8; d++) {
        for (int step = 1; step < 8; step++) {
            int tsq = sq + dirs[d] * step;
            if (!SQ_VALID(tsq)) break;
            if (!validSlide(sq, tsq)) break;
            int8_t p = state.squares[tsq];
            if (p == 0) continue;
            if (p * bySide > 0) {
                int8_t pt = pieceType(p);
                bool isDiag = (dirs[d] == -9 || dirs[d] == -7 || dirs[d] == 7 || dirs[d] == 9);
                if (pt == QUEEN) return true;
                if (isDiag && pt == BISHOP) return true;
                if (!isDiag && pt == ROOK) return true;
            }
            break;  // piece blocks further ray
        }
    }
    return false;
}

bool Board::isAttackedByKing(int sq, int8_t bySide) const {
    int f = SQ_FILE(sq), r = SQ_RANK(sq);
    for (int i = 0; i < 8; i++) {
        int tsq = sq + KING_MOVES[i];
        if (!SQ_VALID(tsq)) continue;
        int tf = SQ_FILE(tsq), tr = SQ_RANK(tsq);
        int df = tf > f ? tf - f : f - tf;
        int dr = tr > r ? tr - r : r - tr;
        if (df <= 1 && dr <= 1) {
            int8_t p = state.squares[tsq];
            if (p * bySide > 0 && pieceType(p) == KING) return true;
        }
    }
    return false;
}

bool Board::isAttacked(int sq, int8_t bySide) const {
    if (isAttackedByPawn(sq, bySide)) return true;
    if (isAttackedByKnight(sq, bySide)) return true;
    if (isAttackedBySliding(sq, bySide)) return true;
    if (isAttackedByKing(sq, bySide)) return true;
    return false;
}

bool Board::inCheck(int8_t side) const {
    // Find the king
    int8_t king = (side > 0) ? KING : -KING;
    for (int sq = 0; sq < 64; sq++) {
        if (state.squares[sq] == king) {
            return isAttacked(sq, -side);
        }
    }
    return false;  // no king on board (shouldn't happen)
}

// ── Move execution on the board array ────────────────────────────────────
void Board::makeMoveOnBoard(const Move &m) {
    int8_t piece = state.squares[m.from];
    state.squares[m.to] = (m.promo > 0) ? (piece > 0 ? m.promo : -m.promo) : piece;
    state.squares[m.from] = 0;

    // Handle castling: move the rook too
    int8_t pt = pieceType(piece);
    if (pt == KING) {
        int df = m.to - m.from;
        if (df == 2) {
            // Kingside: rook from h-file to f-file
            state.squares[m.to - 1] = state.squares[m.to + 1];
            state.squares[m.to + 1] = 0;
        } else if (df == -2) {
            // Queenside: rook from a-file to d-file
            state.squares[m.to + 1] = state.squares[m.to - 2];
            state.squares[m.to - 2] = 0;
        }
    }

    // Handle en passant capture
    if (pt == PAWN && m.to == state.epSquare) {
        int capRank = SQ_RANK(m.from);  // captured pawn is on the same rank as the moving pawn
        state.squares[SQ(SQ_FILE(m.to), capRank)] = 0;
    }
}

// ── Make move (full state update) ────────────────────────────────────────
bool Board::makeMove(const Move &m) {
    int8_t piece = state.squares[m.from];
    int8_t pt = pieceType(piece);

    // Update castling rights
    uint8_t oldCastling = state.castling;
    if (pt == KING) {
        if (piece > 0) state.castling &= ~(CASTLE_WK | CASTLE_WQ);
        else           state.castling &= ~(CASTLE_BK | CASTLE_BQ);
    }
    if (pt == ROOK) {
        if (m.from == SQ(0, 0)) state.castling &= ~CASTLE_WQ;
        if (m.from == SQ(7, 0)) state.castling &= ~CASTLE_WK;
        if (m.from == SQ(0, 7)) state.castling &= ~CASTLE_BQ;
        if (m.from == SQ(7, 7)) state.castling &= ~CASTLE_BK;
    }
    // If rook captured on its starting square
    if (m.to == SQ(0, 0)) state.castling &= ~CASTLE_WQ;
    if (m.to == SQ(7, 0)) state.castling &= ~CASTLE_WK;
    if (m.to == SQ(0, 7)) state.castling &= ~CASTLE_BQ;
    if (m.to == SQ(7, 7)) state.castling &= ~CASTLE_BK;

    // Set en passant square
    int8_t oldEp = state.epSquare;
    state.epSquare = -1;
    if (pt == PAWN) {
        int dr = SQ_RANK(m.to) - SQ_RANK(m.from);
        if (dr == 2 || dr == -2) {
            state.epSquare = SQ(SQ_FILE(m.from), (SQ_RANK(m.from) + SQ_RANK(m.to)) / 2);
        }
    }

    // Half-move clock
    if (pt == PAWN || m.captured != 0) state.halfMoveClock = 0;
    else state.halfMoveClock++;

    // Execute on board
    makeMoveOnBoard(m);

    // Switch sides
    state.sideToMove = (state.sideToMove > 0) ? -1 : 1;
    if (state.sideToMove > 0) state.fullMoveNumber++;

    // Check that own king isn't in check after the move (illegal move)
    if (inCheck((state.sideToMove > 0) ? -1 : 1)) {
        // Undo
        state.sideToMove = (state.sideToMove > 0) ? -1 : 1;
        if (state.sideToMove > 0) state.fullMoveNumber--;
        state.castling = oldCastling;
        state.epSquare = oldEp;
        // Undo board
        state.squares[m.from] = piece;
        // EP capture: destination was empty, captured pawn was on an adjacent square
        if (pt == PAWN && m.captured != 0 && m.to == oldEp) {
            state.squares[m.to] = 0;  // destination was empty before the move
            int capRank = SQ_RANK(m.from);
            state.squares[SQ(SQ_FILE(m.to), capRank)] = m.captured;
        } else {
            state.squares[m.to] = m.captured;
        }
        if (pt == KING) {
            int df = m.to - m.from;
            if (df == 2) { state.squares[m.to + 1] = state.squares[m.to - 1]; state.squares[m.to - 1] = 0; }
            if (df == -2) { state.squares[m.to - 2] = state.squares[m.to + 1]; state.squares[m.to + 1] = 0; }
        }
        return false;
    }

    return true;
}

// ── Undo move ────────────────────────────────────────────────────────────
void Board::undoMove(const Move &m, const BoardState &prev) {
    memcpy(&state, &prev, sizeof(BoardState));
}

// ── Move generation: pawns ───────────────────────────────────────────────
void Board::addPawnMoves(Move *moves, int &count, int maxMoves) const {
    int8_t side = state.sideToMove;
    int dir = (side > 0) ? 8 : -8;        // forward direction
    int startRank = (side > 0) ? 1 : 6;    // starting rank
    int promoRank = (side > 0) ? 6 : 1;    // rank before promotion

    for (int sq = 0; sq < 64 && count < maxMoves; sq++) {
        int8_t p = state.squares[sq];
        if (p * side <= 0 || pieceType(p) != PAWN) continue;

        int r = SQ_RANK(sq), f = SQ_FILE(sq);

        // Single push
        int tsq = sq + dir;
        if (SQ_VALID(tsq) && state.squares[tsq] == 0) {
            if (r == promoRank) {
                // Promotion
                for (int8_t pp = QUEEN; pp >= KNIGHT && count < maxMoves; pp--) {
                    moves[count++] = Move(sq, tsq, pp);
                }
            } else {
                moves[count++] = Move(sq, tsq);
            }
            // Double push from start rank
            if (r == startRank) {
                int tsq2 = sq + dir * 2;
                if (state.squares[tsq2] == 0) {
                    moves[count++] = Move(sq, tsq2);
                }
            }
        }

        // Captures
        for (int df = -1; df <= 1; df += 2) {
            int tf = f + df;
            if (tf < 0 || tf > 7) continue;
            tsq = sq + dir + df;
            if (!SQ_VALID(tsq)) continue;
            // Normal capture
            int8_t target = state.squares[tsq];
            if (target != 0 && target * side < 0) {
                if (r == promoRank) {
                    for (int8_t pp = QUEEN; pp >= KNIGHT && count < maxMoves; pp--) {
                        moves[count++] = Move(sq, tsq, pp, target);
                    }
                } else {
                    moves[count++] = Move(sq, tsq, 0, target);
                }
            }
            // En passant
            if (tsq == state.epSquare) {
                int capSq = SQ(tf, r);
                int8_t capPiece = state.squares[capSq];
                moves[count++] = Move(sq, tsq, 0, capPiece);
            }
        }
    }
}

// ── Move generation: knights ─────────────────────────────────────────────
void Board::addKnightMoves(Move *moves, int &count, int maxMoves) const {
    int8_t side = state.sideToMove;
    for (int sq = 0; sq < 64 && count < maxMoves; sq++) {
        int8_t p = state.squares[sq];
        if (p * side <= 0 || pieceType(p) != KNIGHT) continue;

        int f = SQ_FILE(sq), r = SQ_RANK(sq);
        for (int i = 0; i < 8 && count < maxMoves; i++) {
            int tsq = sq + KNIGHT_MOVES[i];
            if (!SQ_VALID(tsq)) continue;
            int tf = SQ_FILE(tsq), tr = SQ_RANK(tsq);
            int df = tf > f ? tf - f : f - tf;
            int dr = tr > r ? tr - r : r - tr;
            if (!((df == 1 && dr == 2) || (df == 2 && dr == 1))) continue;
            int8_t target = state.squares[tsq];
            if (target * side <= 0) {  // empty or enemy
                moves[count++] = Move(sq, tsq, 0, target);
            }
        }
    }
}

// ── Move generation: sliding pieces ──────────────────────────────────────
void Board::addSlidingMoves(Move *moves, int &count, int maxMoves) const {
    int8_t side = state.sideToMove;
    for (int sq = 0; sq < 64 && count < maxMoves; sq++) {
        int8_t p = state.squares[sq];
        if (p * side <= 0 || !isSliding(p)) continue;

        int8_t pt = pieceType(p);
        const int8_t *dirs;
        int nDirs;
        if (pt == BISHOP) { dirs = DIR_BISHOP; nDirs = 4; }
        else if (pt == ROOK) { dirs = DIR_ROOK; nDirs = 4; }
        else { dirs = DIR_QUEEN; nDirs = 8; }

        for (int d = 0; d < nDirs && count < maxMoves; d++) {
            for (int step = 1; step < 8 && count < maxMoves; step++) {
                int tsq = sq + dirs[d] * step;
                if (!SQ_VALID(tsq)) break;
                if (!validSlide(sq, tsq)) break;
                int8_t target = state.squares[tsq];
                if (target == 0) {
                    moves[count++] = Move(sq, tsq);
                } else {
                    if (target * side < 0) {
                        moves[count++] = Move(sq, tsq, 0, target);
                    }
                    break;
                }
            }
        }
    }
}

// ── Move generation: king ────────────────────────────────────────────────
void Board::addKingMoves(Move *moves, int &count, int maxMoves) const {
    int8_t side = state.sideToMove;
    for (int sq = 0; sq < 64 && count < maxMoves; sq++) {
        int8_t p = state.squares[sq];
        if (p * side <= 0 || pieceType(p) != KING) continue;

        int f = SQ_FILE(sq), r = SQ_RANK(sq);

        // Normal moves
        for (int i = 0; i < 8 && count < maxMoves; i++) {
            int tsq = sq + KING_MOVES[i];
            if (!SQ_VALID(tsq)) continue;
            int tf = SQ_FILE(tsq), tr = SQ_RANK(tsq);
            int df = tf > f ? tf - f : f - tf;
            int dr = tr > r ? tr - r : r - tr;
            if (df > 1 || dr > 1) continue;
            int8_t target = state.squares[tsq];
            if (target * side <= 0) {
                moves[count++] = Move(sq, tsq, 0, target);
            }
        }

        // Castling
        if (side > 0) {
            // White kingside
            if ((state.castling & CASTLE_WK) &&
                state.squares[SQ(5, 0)] == 0 && state.squares[SQ(6, 0)] == 0 &&
                state.squares[SQ(7, 0)] == ROOK &&
                !isAttacked(SQ(4, 0), -1) && !isAttacked(SQ(5, 0), -1) && !isAttacked(SQ(6, 0), -1)) {
                moves[count++] = Move(SQ(4, 0), SQ(6, 0));
            }
            // White queenside
            if ((state.castling & CASTLE_WQ) &&
                state.squares[SQ(3, 0)] == 0 && state.squares[SQ(2, 0)] == 0 && state.squares[SQ(1, 0)] == 0 &&
                state.squares[SQ(0, 0)] == ROOK &&
                !isAttacked(SQ(4, 0), -1) && !isAttacked(SQ(3, 0), -1) && !isAttacked(SQ(2, 0), -1)) {
                moves[count++] = Move(SQ(4, 0), SQ(2, 0));
            }
        } else {
            // Black kingside
            if ((state.castling & CASTLE_BK) &&
                state.squares[SQ(5, 7)] == 0 && state.squares[SQ(6, 7)] == 0 &&
                state.squares[SQ(7, 7)] == -ROOK &&
                !isAttacked(SQ(4, 7), 1) && !isAttacked(SQ(5, 7), 1) && !isAttacked(SQ(6, 7), 1)) {
                moves[count++] = Move(SQ(4, 7), SQ(6, 7));
            }
            // Black queenside
            if ((state.castling & CASTLE_BQ) &&
                state.squares[SQ(3, 7)] == 0 && state.squares[SQ(2, 7)] == 0 && state.squares[SQ(1, 7)] == 0 &&
                state.squares[SQ(0, 7)] == -ROOK &&
                !isAttacked(SQ(4, 7), 1) && !isAttacked(SQ(3, 7), 1) && !isAttacked(SQ(2, 7), 1)) {
                moves[count++] = Move(SQ(4, 7), SQ(2, 7));
            }
        }
    }
}

// ── Pseudo-legal move generation ─────────────────────────────────────────
int Board::genPseudoMoves(Move *moves, int maxMoves) const {
    int count = 0;
    addPawnMoves(moves, count, maxMoves);
    addKnightMoves(moves, count, maxMoves);
    addSlidingMoves(moves, count, maxMoves);
    addKingMoves(moves, count, maxMoves);
    return count;
}

// ── Legal move check ─────────────────────────────────────────────────────
bool Board::isLegalMove(const Move &m) const {
    // Make a copy, try the move
    Board copy = *this;
    return copy.makeMove(m);
}

// ── Full legal move generation ───────────────────────────────────────────
int Board::genMoves(Move *moves, int maxMoves) const {
    Move pseudo[256];
    int n = genPseudoMoves(pseudo, 256);
    int count = 0;
    for (int i = 0; i < n && count < maxMoves; i++) {
        if (isLegalMove(pseudo[i])) {
            moves[count++] = pseudo[i];
        }
    }
    return count;
}

// ── Game state ───────────────────────────────────────────────────────────

bool Board::isCheckmate() const {
    if (!inCheck()) return false;
    Move m;
    return genMoves(&m, 1) == 0;
}

bool Board::isStalemate() const {
    if (inCheck()) return false;
    Move m;
    return genMoves(&m, 1) == 0;
}

int Board::getResult() const {
    if (isCheckmate()) {
        return (state.sideToMove > 0) ? RESULT_BLACK_WIN : RESULT_WHITE_WIN;
    }
    if (isStalemate()) return RESULT_DRAW;
    // 50-move rule
    if (state.halfMoveClock >= 100) return RESULT_DRAW;
    // TODO: insufficient material, threefold repetition
    return RESULT_NONE;
}

// ── Display character ────────────────────────────────────────────────────
char Board::pieceChar(int8_t p) {
    if (p == 0) return '.';
    char c;
    switch (pieceType(p)) {
        case PAWN:   c = 'P'; break;
        case KNIGHT: c = 'N'; break;
        case BISHOP: c = 'B'; break;
        case ROOK:   c = 'R'; break;
        case QUEEN:  c = 'Q'; break;
        case KING:   c = 'K'; break;
        default:     c = '?'; break;
    }
    return isWhite(p) ? c : (c + 32);  // lowercase for black
}
