// sverity-args: --top top
// expect: LNT-032 WARNING 8
module top(input logic [1:0] s, output logic y);
  always_comb begin
    y = 1'b0;
    case (s) inside
      [2'd0:2'd1]: y = 1'b1;
      [2'd1:2'd3]: y = 1'b0;
      default: y = 1'b0;
    endcase
  end
endmodule
