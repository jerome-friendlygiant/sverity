// sverity-args: --top top
// expect: LNT-032 WARNING 10
// [-2:-1] overlaps the last item under one typing only, but 4'sd5 overlaps it under both: the overlap is certain.
module top(input logic signed [3:0] s, output logic y);
  always_comb begin
    y = 1'b0;
    case (s) inside
      [-2:-1]: y = 1'b1;
      4'sd5: y = 1'b1;
      [4'd5:4'd15]: y = 1'b0;
      default: y = 1'b0;
    endcase
  end
endmodule
