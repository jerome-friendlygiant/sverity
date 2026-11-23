// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-009 WARNING
// expect-unresolved: CDC-009
// expect-none: CDC-017
module opaque_map(input logic [3:0] data_i, output logic [3:0] data_o);
  // A run-time branch in the body: the function is not straight-line, so its bit mapping stays opaque.
  function automatic logic [3:0] transform(input logic [3:0] value, input logic swap);
    if (swap) transform = {value[2:0], value[3]};
    else transform = value;
  endfunction
  assign data_o = transform(data_i, data_i[0]);
endmodule

module lane_sync(input logic clk_i, rst_ni, data_i, output logic data_o);
  logic first_q;
  always_ff @(posedge clk_i or negedge rst_ni)
    if (!rst_ni) begin first_q <= 1'b0; data_o <= 1'b0; end
    else begin first_q <= data_i; data_o <= first_q; end
endmodule

module top(input logic clka, clkb, rst_n, inc, output logic [3:0] q);
  logic [3:0] binary_q, mapped;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) binary_q <= '0;
    else if (inc) binary_q <= binary_q + 1'b1;
  opaque_map i_map(binary_q, mapped);
  for (genvar i = 0; i < 4; ++i) begin : gen_sync
    lane_sync u_lane(clkb, rst_n, mapped[i], q[i]);
  end
endmodule
