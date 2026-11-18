// sverity-args: --top top
// The checked spelling of the same intent, and a comment that merely mentions a pragma.
// expect-none: LNT-083
module top(input logic [1:0] sel, input logic a, b, c, output logic y);
  // use unique case instead of the synopsys full_case pragma
  always_comb begin
    unique case (sel)
      2'b00: y = a;
      2'b01: y = b;
      default: y = c;
    endcase
  end
endmodule
