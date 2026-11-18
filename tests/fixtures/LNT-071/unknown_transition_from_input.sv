// sverity-args: --top top
// One arm loads the next state from an input: where the machine can go is not known, so reachability and deadlock stay an open question.
// expect-unresolved: LNT-071 10
module top(input logic clk, rst_n, go, input logic [1:0] code, output logic busy);
  typedef enum logic [1:0] {IDLE, RUN, DONE} st_t;
  st_t st, nx;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) st <= IDLE; else st <= nx;
  always_comb begin
    nx = st;
    case (st)
      IDLE: if (go) nx = st_t'(code);
      RUN: nx = DONE;
      DONE: nx = IDLE;
      default: nx = IDLE;
    endcase
  end
  assign busy = st == RUN;
endmodule
