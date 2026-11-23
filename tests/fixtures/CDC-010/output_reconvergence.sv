(* black_box *) module opaque_source(output logic value);
endmodule

module top(input logic clk_src, clk_dst, a, b,
           output logic xor_out, output logic [1:0] packed_word,
           output logic [1:0] repeated, output logic [1:0] padded,
           output logic out_a, out_b, output logic [1:0] opaque_mix);
  logic a1, a2, b1, b2, opaque_value;
  opaque_source u_opaque(.value(opaque_value));
  always_ff @(posedge clk_dst) begin
    a1 <= a; a2 <= a1;
    b1 <= b; b2 <= b1;
  end
  assign xor_out = a2 ^ b2;
  assign packed_word = {a2, b2};
  assign repeated = {a2, a2};
  assign padded = {1'b0, a2};
  assign out_a = a2;
  assign out_b = b2;
  assign opaque_mix = {opaque_value, a2};
endmodule
