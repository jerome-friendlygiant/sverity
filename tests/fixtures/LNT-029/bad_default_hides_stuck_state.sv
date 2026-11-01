// sverity-args: --top top
// a default arm does not give RUN a transition; RUN holds forever
// expect: LNT-029 WARNING
module top(input logic clk, rst_n, go, output logic busy);
  typedef enum logic [1:0] {IDLE, RUN, DONE} st_t;
  st_t st;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) st <= IDLE;
    else case (st)
      IDLE: if (go) st <= RUN;
      RUN: busy <= 1'b1;
      DONE: st <= IDLE;
      default: st <= IDLE;
    endcase
endmodule
