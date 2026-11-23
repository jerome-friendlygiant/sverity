// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: assign ptr_o = binary_q; => assign ptr_o = ptr_q;
// revert-expect-none: CDC-009
// revert-expect-none: CDC-017
// expect: CDC-017 ERROR
// The source counter, encoder alias, and per-bit capture lanes are separate modules.
module gray_encode #(parameter int W = 4) (input logic [W-1:0] binary_i, output logic [W-1:0] gray_o);
  logic [W:0] extended;
  assign extended = {1'b0, binary_i};
  assign gray_o = binary_i ^ extended[W:1];
endmodule

module pointer_source #(parameter int W = 4, parameter bit USE_GRAY = 0)
  (input logic clk_i, rst_ni, inc_i, output logic [W-1:0] ptr_o);
  logic [W-1:0] binary_q, ptr_q, gray, binary_next;
  always_ff @(posedge clk_i or negedge rst_ni)
    if (!rst_ni) binary_q <= '0;
    else if (inc_i) binary_q <= binary_q + 1'b1;
  assign binary_next = binary_q + 1'b1;
  gray_encode #(.W(W)) i_encode (.binary_i(binary_next), .gray_o(gray));
  always_ff @(posedge clk_i or negedge rst_ni)
    if (!rst_ni) ptr_q <= '0;
    else if (inc_i) ptr_q <= gray;
  if (USE_GRAY) begin : gen_gray_value
    assign ptr_o = ptr_q;
  end else begin : gen_binary_value
    assign ptr_o = binary_q;
  end
endmodule

module sync_lane(input logic clk_i, rst_ni, data_i, output logic data_o);
  logic first_q;
  always_ff @(posedge clk_i or negedge rst_ni)
    if (!rst_ni) begin first_q <= 1'b0; data_o <= 1'b0; end
    else begin first_q <= data_i; data_o <= first_q; end
endmodule

module top(input logic clka, clkb, rst_n, inc, output logic [3:0] q_gray, q_binary);
  logic [3:0] gray_ptr, binary_ptr;
  pointer_source #(.USE_GRAY(1)) u_gray_src(clka, rst_n, inc, gray_ptr);
  pointer_source #(.USE_GRAY(0)) u_binary_src(clka, rst_n, inc, binary_ptr);
  for (genvar i = 0; i < 4; ++i) begin : gen_gray_sync
    sync_lane i_lane(clkb, rst_n, gray_ptr[i], q_gray[i]);
  end
  for (genvar i = 0; i < 4; ++i) begin : gen_binary_sync
    sync_lane i_lane(clkb, rst_n, binary_ptr[i], q_binary[i]);
  end
endmodule
