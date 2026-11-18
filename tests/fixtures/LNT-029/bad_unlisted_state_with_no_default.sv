// sverity-args: --top top
// RETRY is entered and has no item, and nothing but the hold before the case assigns the next state there: it is never left.
// expect: LNT-029 WARNING 10
module top(input logic clk, rst_n, go, nack, output logic busy);
  typedef enum logic [2:0] {IDLE, WAIT, RETRY, DONE} st_t;
  st_t st, nx;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) st <= IDLE; else st <= nx;
  always_comb begin
    nx = st;
    case (st)
      IDLE: if (go) nx = WAIT;
      WAIT: nx = nack ? RETRY : DONE;
      DONE: nx = IDLE;
    endcase
  end
  assign busy = st == WAIT;
endmodule
