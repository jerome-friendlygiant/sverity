// sverity-args: --top top
// RETRY is a target of WAIT but has no item, and the default keeps the state: once entered it is never left.
// expect: LNT-029 WARNING 12
// revert: default: nx = st; => default: nx = IDLE;
// revert-expect-none: LNT-029 WARNING
module top(input logic clk, rst_n, go, nack, output logic busy);
  typedef enum logic [2:0] {IDLE, WAIT, RETRY, DONE} st_t;
  st_t st, nx;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) st <= IDLE; else st <= nx;
  always_comb begin
    nx = st;
    case (st)
      IDLE: if (go) nx = WAIT;
      WAIT: begin
        if (nack) nx = RETRY;
        else nx = DONE;
      end
      DONE: nx = IDLE;
      default: nx = st;
    endcase
  end
  assign busy = st == WAIT;
endmodule
