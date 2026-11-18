// sverity-args: --top top
// The wire is derived from the state by arithmetic that is not evaluated per state: the guard may or may not leave DONE, so the question stays open.
// expect-unresolved: LNT-071 12
module top(input logic clk, rst_n, go, output logic busy);
  typedef enum logic [1:0] {IDLE, RUN, DONE} st_t;
  st_t st, nx;
  logic late;
  assign late = ((st ^ 2'd1) == 2'd3) ? go : 1'b0;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) st <= IDLE; else st <= nx;
  always_comb begin
    nx = st;
    case (st)
      IDLE: if (go) nx = RUN;
      RUN: nx = DONE;
      default: nx = st;
    endcase
    if (late && go) nx = RUN;
  end
  assign busy = st == RUN;
endmodule
