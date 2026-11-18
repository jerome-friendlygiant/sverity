// sverity-args: --top top
// A constant item equal to the case expression always matches when reached, so every later item and the default can
// never run: they are reported as the known branch leaves them unreachable.
// expect: LNT-018 WARNING 8
module top(input logic is_a, is_b, output logic [1:0] sel);
  always_comb begin
    sel = 2'd0;
    unique case (1'b1)
      1'b1:    sel = 2'd1;
      is_a:    sel = 2'd2;
      is_b:    sel = 2'd3;
      default: sel = 2'd0;
    endcase
  end
endmodule
