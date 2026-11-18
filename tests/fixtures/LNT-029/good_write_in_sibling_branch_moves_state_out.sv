// sverity-args: --top top
// The abort branch writes the state in the other branch of the enclosing if: from WAITS too, so the arm is not stuck.
// expect-none: LNT-029
module top(input logic clk, rst_n, go, abort, output logic led);
  typedef enum logic [1:0] {IDLE, RUN, WAITS, DONE} st_t;
  st_t st;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) st <= IDLE;
    else if (abort) st <= DONE;
    else case (st)
      IDLE: if (go) st <= RUN;
      RUN: st <= WAITS;
      WAITS: led <= 1'b1;
      DONE: st <= IDLE;
    endcase
endmodule
