// sverity-args: --top top
// Lookalike of the decoded-wire override: the wire decodes DONE, so the override is the exit from DONE.
// expect-none: LNT-029 WARNING
// expect-none: LNT-071
module top(input logic clk, rst_n, go, output logic busy);
  typedef enum logic [1:0] {IDLE, RUN, DONE} st_t;
  st_t st, nx;
  logic in_done;
  assign in_done = (st == DONE);
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) st <= IDLE; else st <= nx;
  always_comb begin
    nx = st;
    case (st)
      IDLE: if (go) nx = RUN;
      RUN: nx = DONE;
      default: nx = st;
    endcase
    if (in_done && go) nx = IDLE;
  end
  assign busy = st == RUN;
endmodule
