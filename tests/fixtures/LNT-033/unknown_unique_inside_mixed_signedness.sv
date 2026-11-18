// sverity-args: --top top
// expect: LNT-033 WARNING 10
// The LRM types each range alone: [-2:-1] is signed (14, 15 of the 4-bit selector) and [4'd14:4'd15] is unsigned, so both match 14.
// slang types the whole set as one unsigned expression, where [-2:-1] is 32-bit and matches neither. The readings disagree, so the overlap is not decided.
module top(input logic signed [3:0] s, output logic y);
  always_comb begin
    y = 1'b0;
    unique case (s) inside
      [-2:-1]: y = 1'b1;
      [4'd14:4'd15]: y = 1'b0;
      default: y = 1'b0;
    endcase
  end
endmodule
