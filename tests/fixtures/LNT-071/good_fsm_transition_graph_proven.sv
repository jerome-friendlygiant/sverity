// sverity-args: --top top
// Every state is reachable from the reset state through the constant transitions of the case and every one has an exit: the transition
// graph answers the reachability and deadlock question, so no obligation is left, and no dead-state warning.
// expect-none: LNT-071
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
