// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A combinational next-state block with a call writing through an output argument stays Unresolved.
// expect: CDC-039 WARNING
// expect-unresolved: CDC-039
// expect-none: CDC-028 ERROR
// expect-coverage: payload-qualification INCOMPLETE
module top(input logic clka, clkb, rst_n, load, local_en, input logic [3:0] d1, d2, output logic [3:0] q);
  logic [3:0] data, other_data, data_d;
  logic v_a, v1, v2;
  function automatic void copy_val(output logic [3:0] dst, input logic [3:0] src);
    dst = src;
  endfunction
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) begin data <= '0; other_data <= '0; v_a <= 1'b0; end
    else begin data <= d1; other_data <= d2; v_a <= load; end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {v2, v1} <= '0; else {v2, v1} <= {v1, v_a};
  always_comb begin
    data_d = q;
    if (v2) data_d = data;
    if (local_en) copy_val(data_d, other_data);
  end
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) q <= '0; else q <= data_d;
endmodule
