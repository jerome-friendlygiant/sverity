// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-009 WARNING
// expect-none: CDC-033 INFO
// Regression from the independent audit: this hazard must never be classified as safe.
// "Gray" register that is loaded with arbitrary data in one branch: marked Gray by symbol.
module top(input logic clka, clkb, mode, input logic [3:0] din, output logic [3:0] q);
  logic [3:0] bin, g;
  logic [3:0] s1, s2;
  always_ff @(posedge clka) begin
    bin <= bin + 1'b1;
    if (mode) g <= bin ^ (bin >> 1);
    else      g <= din;               // arbitrary multi-bit value
  end
  always_ff @(posedge clkb) begin s1 <= g; s2 <= s1; end
  assign q = s2;
endmodule
