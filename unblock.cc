#include <stdint.h>
#include <assert.h>

#include <iostream>
#include <vector>
#include <sstream>
#include <deque>
#include <unordered_map>
#include <thread>
#include <chrono>

using namespace std;
constexpr size_t kHashMult = 31;
constexpr int kBoardSize = 6;
typedef char DrawBuf[kBoardSize][kBoardSize];
static_assert(sizeof(DrawBuf) == kBoardSize * kBoardSize, "Invalid size of DrawBuf");
constexpr char kEmpty = '.';
constexpr bool kDebug = false;

string BufToString(const DrawBuf& buf) {
  string result;
  for (int y = 0; y < kBoardSize; ++y) {
    for (int x = 0; x < kBoardSize; ++x) {
      result += buf[y][x];
    }
    result += "\n";
  }
  return result;
}

enum class Orient : int8_t {
  kHoriz,
  kVert
};

struct Piece {
  int8_t x = 0;
  int8_t y = 0;
  int8_t size = 2;
  Orient orient = Orient::kHoriz;

  Piece(int x_, int y_, int size_, Orient orient_)
      : x(x_),
        y(y_),
        size(size_),
        orient(orient_) {
  }

  size_t hash() const {
    return x + kHashMult * (y + kHashMult * (size + kHashMult * static_cast<size_t>(orient)));
  }

  bool operator<(const Piece& other) const {
   return y < other.y || (y == other.y && x < other.x);
  }

  void Draw(DrawBuf* buf, char c) const {
    int cur_x = x;
    int cur_y = y;
    for (int i = 0; i < size; ++i) {
      if ((*buf)[cur_y][cur_x] != kEmpty) {
        cerr << "Clash at coordinates x=" << cur_x << ", y=" << cur_y
             << " when drawing " << ToString() 
             << ", found: " << (*buf)[cur_y][cur_x] << endl;
        cerr << "Current state of board:" << endl << BufToString(*buf) << endl;
        exit(1);
      }
      (*buf)[cur_y][cur_x] = c;
      if (orient == Orient::kHoriz) {
        ++cur_x;
      } else {
        ++cur_y;
      }
    }
  }

  string ToString() const {
    ostringstream ss;
    ss << "Piece(x=" << static_cast<int>(x) << ", y=" << static_cast<int>(y) << ", size=" 
       << static_cast<int>(size) << ", orient=" << 
        (orient == Orient::kHoriz ? "horiz" : "vert") << ")";
    return ss.str();
  }

  const bool operator==(const Piece& other) const {
    return x == other.x && y == other.y && size == other.size && orient == other.orient;
  }

  pair<int, int> MovementRange(const DrawBuf& buf) const {
    int min_delta = 0;
    int max_delta = 0;
    if (orient == Orient::kHoriz) {
      int cur_x = x - 1;
      while (min_delta > -1 && cur_x >= 0 && buf[y][cur_x] == kEmpty) {
        min_delta--;
        cur_x--;
      }
      cur_x = x + size;
      while (max_delta < 1 && cur_x < kBoardSize && buf[y][cur_x] == kEmpty) {
        max_delta++;
        cur_x++;
      }
    } else {
      int cur_y = y - 1;
      while (min_delta > -1 && cur_y >= 0 && buf[cur_y][x] == kEmpty) {
        min_delta--;
        cur_y--;
      }
      cur_y = y + size;
      while (max_delta < 1 && cur_y < kBoardSize && buf[cur_y][x] == kEmpty) {
        max_delta++;
        cur_y++;
      }
    }
    return {min_delta, max_delta};
  }

  void Move(int delta) {
    if (orient == Orient::kHoriz) {
      x += delta;
    } else {
      y += delta;
    }
  }
};

struct State { 
  vector<Piece> pieces;

  size_t hash() const {
    size_t cur = 0;
    for (const auto& piece : pieces) {
      cur = cur * kHashMult + piece.hash();
      cur ^= cur << 32;
    }
    return cur;
  }

  void Canonicalize() {
   sort(pieces.begin() + 1, pieces.end());
  }

  void Draw(DrawBuf* b) const {
    memset(b, kEmpty, sizeof(*b));
    int i = 0;
    for (const auto& p : pieces) {
      if (i == 0) {
        p.Draw(b, '*');
      } else {
        p.Draw(b, 'A' + i);
      }
      ++i;
    }
  }

  string ToString() const {
    DrawBuf b;
    Draw(&b);
    return BufToString(b);
  }

  bool operator==(const State& other) const {
    return pieces == other.pieces;
  }

  vector<State> Neighbors() {
    vector<State> result;
    DrawBuf b;
    Draw(&b);
    int i = 0;
    for (const auto& p : pieces) {
      auto range = p.MovementRange(b);
      if (kDebug) {
        cout << "first=" << range.first << ", second=" << range.second << ", p=" << p.ToString() << endl;
      }
      for (int delta = range.first; delta <= range.second; ++delta) {
        if (delta != 0) {
          State neighbor = *this;
          neighbor.pieces[i].Move(delta);
          neighbor.Canonicalize();
          result.push_back(std::move(neighbor));
        }
      }
      ++i;
    }
    return std::move(result);
  }
};

struct StateHash {
  typedef State argument_type;
  typedef std::size_t result_type; 
  result_type operator()(argument_type const& s) const noexcept {
    return s.hash();
  }
};

struct QueueElem {
 State state;
 int moves = 0;
};

class Game {
 public:
  Game(State start_state, int dest_x, int dest_y)
      : start_state_(start_state),
        dest_x_(dest_x),
        dest_y_(dest_y) {
  }
  
  vector<State> Solve() {
    queue_.push_back({start_state_, 0});
    prev_state_.emplace(start_state_, State());
    while (!queue_.empty()) {
      auto elem = std::move(queue_.front());
      queue_.pop_front();
      if (kDebug) {
        cout << "This state is achievable in " << elem.moves << ":" << endl << elem.state.ToString() << endl << endl;
      }
      const auto& front_piece = elem.state.pieces.front();
      if (front_piece.x == dest_x_ && front_piece.y == dest_y_) {
        return TracePathTo(elem.state);
      }

      for (auto new_state : elem.state.Neighbors()) {
        if (prev_state_.find(new_state) == prev_state_.end()) {
          prev_state_.emplace(new_state, elem.state);
          queue_.push_back({new_state, elem.moves + 1});
        }
      }
    }
    return vector<State>();
  };

  vector<State> TracePathTo(const State& final_state) {
    State s = final_state;
    vector<State> sequence;
    while (!s.pieces.empty()) {
      sequence.emplace_back(s);
      auto iter = prev_state_.find(s);
      if (iter == prev_state_.end()) {
        cerr << "prev_state_ is not defined for key:\n" << s.ToString() << endl;
        exit(1);
      }
      s = std::move(iter->second);
    }
    reverse(sequence.begin(), sequence.end());
    return sequence;
  }

 private:
  State start_state_;
  int dest_x_;
  int dest_y_;
  deque<QueueElem> queue_;
  unordered_map<State, State, StateHash> prev_state_;
};

int main(int argc, char** argv) {
  State s;
  s.pieces = {
      { 0, 2, 2, Orient::kHoriz },
      { 3, 0, 2, Orient::kVert },
      { 4, 0, 2, Orient::kHoriz },
      { 4, 1, 2, Orient::kHoriz },
      { 3, 2, 2, Orient::kVert },
      { 0, 4, 2, Orient::kVert },
      { 1, 4, 2, Orient::kVert },
      { 3, 4, 2, Orient::kHoriz },
      { 5, 3, 2, Orient::kVert }
  };
  Game g(s, 4, 2);
  auto solution = g.Solve();
  for (const auto& state : solution) {
    cout << "\033[2J";
    cout << state.ToString() << endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
  cout << "Moves: " << (solution.size() - 1) << endl;
  return 0;
}

