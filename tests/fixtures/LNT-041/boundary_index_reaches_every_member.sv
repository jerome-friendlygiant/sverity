// sverity-args: --top top
// A write through a run-time index whose range holds every element drives each of them in turn; the whole write before it changes nothing.
// (Which elements a latch holds is LNT-013's question, not an undriven member.)
// expect-none: LNT-041
module top(input logic [1:0] i, input logic a, output logic y);
  logic [1:0] m [2];
  always_comb begin
    m[0] = 2'b00;
    m[i] = {a, a};
  end
  assign y = m[0][0];
endmodule
