// sverity-args: --top top
// WAIT assigns no state in its arm, but a write after the case with no state guard can leave any state: not stuck.
// expect-none: LNT-029
module top(input logic clk, rst_n, go, tick, output logic busy, wait_o);
  typedef enum logic [1:0] {IDLE, RUN, WAIT, DONE} st_t;
  st_t st, nx;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) st <= IDLE; else st <= nx;
  always_comb begin
    nx = st; wait_o = 0;
    case (st)
      IDLE: if (go) nx = RUN;
      RUN: nx = WAIT;
      WAIT: wait_o = 1;
      DONE: nx = IDLE;
    endcase
    if (tick) nx = DONE;
  end
  assign busy = st == RUN;
endmodule
