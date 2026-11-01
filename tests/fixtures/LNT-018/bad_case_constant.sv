// sverity-args: --top top
// expect: LNT-018 WARNING 6
module top(input logic a, output logic y);
  always_comb begin
    y = a;
    case (2'd1)
      2'd0: y = 1'b0;
      2'd1: y = ~a;
      default: y = 1'b1;
    endcase
  end
endmodule
