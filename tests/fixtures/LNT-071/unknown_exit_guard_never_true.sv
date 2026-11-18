// sverity-args: --top top
// The only exit of RUN is guarded by `go && !go`, which is never true: RUN keeps itself.
// expect-unresolved: LNT-071 10
module top(input logic clk, rst_n, go, output logic busy);
  typedef enum logic [1:0] {IDLE, RUN, DONE} st_t;
  st_t st, nx;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) st <= IDLE; else st <= nx;
  always_comb begin
    nx = st;
    case (st)
      IDLE: if (go) nx = RUN;
      RUN: if (go && !go) nx = DONE;
      DONE: nx = IDLE;
      default: nx = IDLE;
    endcase
  end
  assign busy = st == RUN;
endmodule
