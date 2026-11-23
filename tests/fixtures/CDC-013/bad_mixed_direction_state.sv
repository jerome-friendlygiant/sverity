// Distinct writes can both raise and lower q, so no temporal direction is proved.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-013 WARNING
module top(input logic clka, clkb, raise_i, lower_i, output logic seen_o);
  logic q, s1, s2;
  always_ff @(posedge clka) begin
    if (raise_i) q <= q | raise_i;
    else q <= q & lower_i;
  end
  always_ff @(posedge clkb) begin s1 <= q; s2 <= s1; end
  assign seen_o = s2;
endmodule
