// sverity-args: --top top
// A pragma in a comment: simulation keeps `y` for sel = 3 (a latch), synthesis treats it as don't care.
// expect: LNT-083 WARNING 6
module top(input logic [1:0] sel, input logic a, b, c, output logic y);
  always @* begin
    case (sel) // synopsys full_case parallel_case
      2'b00: y = a;
      2'b01: y = b;
      2'b10: y = c;
    endcase
  end
endmodule
