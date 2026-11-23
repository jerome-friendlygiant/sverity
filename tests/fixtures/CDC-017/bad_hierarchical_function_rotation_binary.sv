// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A straight-line function that rotates the bits is an exact one-to-one mapping: each lane synchronises one bit of a binary
// counter, so several bits can change in one capture window.
// expect: CDC-017 ERROR
// expect-none: CDC-009
module opaque_map(input logic [3:0] data_i, output logic [3:0] data_o);
  function automatic logic [3:0] transform(input logic [3:0] value);
    transform = {value[2:0], value[3]};
  endfunction
  assign data_o = transform(data_i);
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
