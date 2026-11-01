// sverity-args: --top top
// A fully connected FSM still carries the reachability obligation (INFO, never proven) and no dead-state warning.
// expect: LNT-029 INFO
// expect-none: LNT-029 WARNING
module top(input logic clk, rst_n, go, output logic busy);
  typedef enum logic [1:0] {IDLE, RUN, DONE} st_t;
  st_t st, nx;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) st <= IDLE; else st <= nx;
  always_comb begin
    nx = st;
    case (st)
      IDLE: if (go) nx = RUN;
      RUN: nx = DONE;
      DONE: nx = IDLE;
      default: nx = IDLE;
    endcase
  end
  assign busy = st == RUN;
endmodule
