// sverity-args: --top top
// RUN is listed and assigns its own value, so nothing leaves it: a terminal state by design, or a stuck one. The graph is read, and
// the deadlock question stays open.
// expect-unresolved: LNT-071 11
module top(input logic clk, rst_n, go, output logic busy);
  typedef enum logic [1:0] {IDLE, RUN, DONE} st_t;
  st_t st, nx;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) st <= IDLE; else st <= nx;
  always_comb begin
    nx = IDLE;
    case (st)
      IDLE: if (go) nx = RUN;
      RUN: nx = RUN;
      DONE: nx = IDLE;
      default: nx = IDLE;
    endcase
  end
  assign busy = st == RUN;
endmodule
