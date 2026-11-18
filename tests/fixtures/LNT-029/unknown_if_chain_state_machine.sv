// sverity-args: --top top
// A state machine coded as an if / else-if chain has no case to read: the gap is reported.
// expect-unresolved: LNT-029 8
module top(input logic clk, rst_n, go, output logic busy);
  typedef enum logic [1:0] {IDLE, RUN, WAIT, NEVER} st_t;
  st_t st;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) st <= IDLE;
    else if (st == IDLE && go) st <= RUN;
    else if (st == RUN) st <= WAIT;
    else if (st == WAIT) st <= IDLE;
  assign busy = st == RUN;
endmodule
