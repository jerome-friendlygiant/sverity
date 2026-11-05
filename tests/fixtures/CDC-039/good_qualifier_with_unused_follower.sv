// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An unused register after the synchroniser does not stop s2 qualifying the capture.
// expect-none: CDC-028
module top(input logic clka, clkb, en, d, output logic y);
  logic valid, payload, s1, s2, s3;
  always_ff @(posedge clka) begin valid <= en; if (en) payload <= d; end
  always_ff @(posedge clkb) begin
    s1 <= valid; s2 <= s1; s3 <= s2;
    if (s2) y <= payload;
  end
endmodule
