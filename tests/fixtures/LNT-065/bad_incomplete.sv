// sverity-args: --top top
// slang's case diagnostics are mapped to this rule (missing default, incomplete coverage)
// expect: LNT-065 WARNING
module top(input logic [1:0] s, output logic [1:0] y);
  always_comb begin
    y = 0;
    case (s)
      2'd0: y = 1;
      2'd1: y = 2;
    endcase
  end
endmodule
