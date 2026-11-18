// sverity-args: --top top
// The selector is one enum and the item the constant of another: the types disagree on what the values mean.
// expect: LNT-005 WARNING 12
module top(input logic c, output logic y);
  typedef enum logic [1:0] {Idle = 2'd0, Run = 2'd1} mode_e;
  typedef enum logic [1:0] {Red = 2'd0, Green = 2'd1} color_e;
  color_e color;
  always_comb begin
    color = c ? Red : Green;
    y = 1'b0;
    case (color)
      Idle: y = 1'b1;
      default: y = 1'b0;
    endcase
  end
endmodule
