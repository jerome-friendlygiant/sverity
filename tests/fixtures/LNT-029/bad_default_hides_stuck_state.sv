// sverity-args: --top top
// A default arm does not give RUN a transition: RUN is listed, so the default never applies to it, and RUN holds forever. Every state is assigned somewhere.
// expect: LNT-029 WARNING 12
// expect-count: LNT-029 WARNING 1
module top(input logic clk, rst_n, go, output logic busy);
  typedef enum logic [1:0] {IDLE, RUN, DONE} st_t;
  st_t st;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) st <= IDLE;
    else case (st)
      IDLE: if (go) st <= RUN; else st <= DONE;
      RUN: busy <= 1'b1;
      DONE: st <= IDLE;
      default: st <= IDLE;
    endcase
endmodule
