module bit_sync #(
  parameter int STAGES = 2
) (
  input  logic clk,
  input  logic arst_n,
  input  logic d,
  output logic q
);
  logic [STAGES-1:0] pipe;
  always_ff @(posedge clk or negedge arst_n) begin
    if (!arst_n) pipe <= '0;
    else pipe <= {pipe[STAGES-2:0], d};
  end
  assign q = pipe[STAGES-1];
endmodule

module state_consumer #(
  parameter int WIDTH = 2
) (
  input  logic clk_dst,
  input  logic arst_n,
  input  logic [WIDTH-1:0] state_async,
  output logic illegal_state,
  output logic active
);
  logic [WIDTH-1:0] state_sync;
  genvar bit_no;
  generate
    for (bit_no = 0; bit_no < WIDTH; bit_no++) begin : g_sync
      bit_sync u_bit (
        .clk(clk_dst), .arst_n(arst_n), .d(state_async[bit_no]),
        .q(state_sync[bit_no])
      );
    end
  endgenerate

  always_ff @(posedge clk_dst or negedge arst_n) begin
    if (!arst_n) begin
      illegal_state <= 1'b0;
      active <= 1'b0;
    end else begin
      illegal_state <= (state_sync == 2'b11);
      active <= (state_sync == 2'b01);
    end
  end
endmodule

module top (
  input  logic src_clk,
  input  logic dst_clk,
  input  logic arst_n,
  input  logic [1:0] src_state,
  output logic illegal_state,
  output logic active
);
  state_consumer #(.WIDTH(2)) u_consumer (
    .clk_dst(dst_clk), .arst_n(arst_n), .state_async(src_state),
    .illegal_state(illegal_state), .active(active)
  );
endmodule
