// sverity-args: --top top
// Every arm of a case with a default writes element i.
// expect-none: LNT-024
module top(input logic [1:0] s, input logic c, output logic [3:0] o);
  always_comb begin
    for (int i = 0; i < 4; i++) begin
      case (s)
        2'd0: o[i] = c;
        2'd1: o[i] = ~c;
        default: o[i] = 1'b0;
      endcase
    end
  end
endmodule
