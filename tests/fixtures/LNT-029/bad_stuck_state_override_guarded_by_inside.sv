// sverity-args: --top top
// `st inside {IDLE}` is true only in IDLE: the override leaves DONE as it was.
// expect: LNT-029 WARNING 10
module top(input logic clk, rst_n, go, output logic busy);
  typedef enum logic [1:0] {IDLE, RUN, DONE} st_t;
  st_t st, nx;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) st <= IDLE; else st <= nx;
  always_comb begin
    nx = st;
    case (st)
      IDLE: if (go) nx = RUN;
      RUN: nx = DONE;
      default: nx = st;
    endcase
    if (st inside {IDLE} && go) nx = RUN;
  end
  assign busy = st == RUN;
endmodule
