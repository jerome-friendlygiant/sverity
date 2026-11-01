// sverity-args: --top top
// expect: LNT-029 WARNING
// expect: LNT-029 INFO
module top(input logic clk, rst_n, go, output logic busy);
  typedef enum logic [1:0] {IDLE, RUN, STUCK, NEVER} st_t;
  st_t st, nx;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) st <= IDLE; else st <= nx;
  always_comb begin
    case (st)
      IDLE: nx = go ? RUN : IDLE;
      RUN: nx = STUCK;
      STUCK: ;
      default: nx = IDLE;
    endcase
  end
  assign busy = st == RUN;
endmodule
