// sverity-args: --top top
// Independent registers share a type; first's LEFT assignment must not satisfy second's missing LEFT transition.
// expect: LNT-029 WARNING
module top(input logic clk, input logic go);
  typedef enum logic [1:0] {LEFT, RIGHT, IDLE} state_t;
  state_t first, second, next_second;
  always_ff @(posedge clk) begin
    first <= go ? LEFT : IDLE;
    second <= next_second;
  end
  always_comb begin
    next_second = second;
    case (second)
      IDLE: next_second = go ? RIGHT : IDLE;
      RIGHT: next_second = IDLE;
    endcase
  end
endmodule
