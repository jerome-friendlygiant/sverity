// sverity-args: --top top
// The reset branch writes the state outside the case, but only while the case does not run: STUCK is left by nothing.
// expect: LNT-029 WARNING 13
// expect-count: LNT-029 WARNING 1
module top(input logic clk, rst_n, go, output logic busy, output logic led);
  typedef enum logic [1:0] {IDLE, RUN, STUCK, DONE} st_t;
  st_t st;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) st <= IDLE;
    else case (st)
      IDLE: if (go) st <= RUN;
      RUN: st <= STUCK;
      STUCK: led <= 1;
      DONE: st <= IDLE;
      default: st <= DONE;
    endcase
  assign busy = st == RUN;
endmodule
