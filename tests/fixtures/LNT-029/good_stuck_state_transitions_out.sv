// sverity-args: --top top
// Near miss of bad_default_hides_stuck_state.sv: RUN now transitions to DONE instead of holding forever, so no state is stuck.
// expect-none: LNT-029
module top(input logic clk, rst_n, go, output logic busy);
  typedef enum logic [1:0] {IDLE, RUN, DONE} st_t;
  st_t st;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) st <= IDLE;
    else case (st)
      IDLE: if (go) st <= RUN;
      RUN: st <= DONE;
      DONE: st <= IDLE;
      default: st <= IDLE;
    endcase
  assign busy = st == RUN;
endmodule
