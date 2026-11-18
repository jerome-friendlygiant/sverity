// sverity-args: --top top
// The transitions are computed by a function: they are not read, and the gap is reported.
// expect-unresolved: LNT-029 8
module top(input logic clk, rst_n, go, output logic busy);
  typedef enum logic [1:0] {IDLE, RUN, WAIT, NEVER} st_t;
  st_t st;
  function automatic st_t next(st_t s, logic g);
    case (s)
      IDLE: next = g ? RUN : IDLE;
      RUN: next = WAIT;
      WAIT: next = IDLE;
      NEVER: next = IDLE;
    endcase
  endfunction
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) st <= IDLE; else st <= next(st, go);
  assign busy = st == RUN;
endmodule
