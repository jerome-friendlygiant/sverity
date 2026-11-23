// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An ungated multi-bit payload whose capture register feeds destination logic remains CDC-016, not CDC-006.
// expect: CDC-016 ERROR
// expect-none: CDC-006
module top(input logic clka, clkb, v_i, input logic [7:0] d_i, mask, output logic [7:0] q);
  logic v_a, v1, v2;
  logic [7:0] d_a, d_b, o_q;
  always_ff @(posedge clka) begin
    v_a <= v_i;
    if (v_i) d_a <= d_i;
  end
  always_ff @(posedge clkb) begin
    v1 <= v_a;
    v2 <= v1;
  end
  always_ff @(posedge clkb) begin
    d_b <= d_a;
    o_q <= d_b & mask;
  end
  assign q = o_q;
endmodule
