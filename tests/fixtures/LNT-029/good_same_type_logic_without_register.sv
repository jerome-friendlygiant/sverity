// sverity-args: --top top
// Same-type mapping logic with no clocked register of the type anywhere is not an FSM.
// expect-none: LNT-029
module top(input logic [1:0] sel, output logic [1:0] y);
  typedef enum logic [1:0] {IDLE, RUN, DONE} mode_t;
  mode_t cur, nxt;
  always_comb begin
    cur = mode_t'(sel);
    nxt = cur;
    case (cur)
      IDLE: nxt = RUN;
      RUN: nxt = DONE;
      default: ;
    endcase
  end
  assign y = nxt;
endmodule
