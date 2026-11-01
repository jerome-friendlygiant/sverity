// sverity-args: --top top
// expect: LNT-032 WARNING 6
module top(input logic [1:0] s, input logic [1:0] k, output logic y);
  always_comb begin
    y = 1'b0;
    case (s) inside
      k: y = 1'b1;
      default: y = 1'b0;
    endcase
  end
endmodule
