// sverity-args: --top top
// `nx = st` is a hold, and the later write applies only in DONE: nothing moves STUCK.
// expect: LNT-029 WARNING 14
// expect-count: LNT-029 WARNING 1
module top(input logic clk, rst_n, go, output logic busy, output logic led);
  typedef enum logic [1:0] {IDLE, RUN, STUCK, DONE} st_t;
  st_t st, nx;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) st <= IDLE; else st <= nx;
  always_comb begin
    nx = st; led = 0;
    case (st)
      IDLE: if (go) nx = RUN;
      RUN: nx = STUCK;
      STUCK: led = 1;
      DONE: nx = IDLE;
    endcase
    if (go && st == DONE) nx = DONE;
  end
  assign busy = st == RUN;
endmodule
