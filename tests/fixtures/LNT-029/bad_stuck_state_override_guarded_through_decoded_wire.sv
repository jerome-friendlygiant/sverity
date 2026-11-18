// sverity-args: --top top
// The override after the case reads the state through a wire that decodes it: it moves only IDLE, so DONE can be entered and never left.
// expect: LNT-029 WARNING 12
module top(input logic clk, rst_n, go, output logic busy);
  typedef enum logic [1:0] {IDLE, RUN, DONE} st_t;
  st_t st, nx;
  logic in_idle;
  assign in_idle = (st == IDLE);
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) st <= IDLE; else st <= nx;
  always_comb begin
    nx = st;
    case (st)
      IDLE: if (go) nx = RUN;
      RUN: nx = DONE;
      default: nx = st;
    endcase
    if (in_idle && go) nx = RUN;
  end
  assign busy = st == RUN;
endmodule
