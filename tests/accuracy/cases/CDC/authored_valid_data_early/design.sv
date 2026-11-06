module packet_source #(
  parameter int WIDTH = 8
) (
  input logic src_clk,
  input logic arst_n,
  input logic send,
  input logic [WIDTH-1:0] data_in,
  output logic valid_async,
  output logic [WIDTH-1:0] data_async
);
  always_ff @(posedge src_clk or negedge arst_n) begin
    if (!arst_n) begin
      valid_async <= 1'b0;
      data_async <= '0;
    end else begin
      valid_async <= send;
      if (send) data_async <= data_in;
    end
  end
endmodule

module packet_sink #(
  parameter int WIDTH = 8
) (
  input logic dst_clk,
  input logic arst_n,
  input logic valid_async,
  input logic [WIDTH-1:0] data_async,
  output logic valid_seen,
  output logic [WIDTH-1:0] data_seen
);
  logic valid_meta;
  logic valid_sync;
  always_ff @(posedge dst_clk or negedge arst_n) begin
    if (!arst_n) begin
      valid_meta <= 1'b0;
      valid_sync <= 1'b0;
      valid_seen <= 1'b0;
      data_seen <= '0;
    end else begin
      valid_meta <= valid_async;
      valid_sync <= valid_meta;
      valid_seen <= valid_sync;
      if (valid_meta) data_seen <= data_async;
    end
  end
endmodule

module top (
  input logic src_clk,
  input logic dst_clk,
  input logic arst_n,
  input logic send,
  input logic [7:0] data_in,
  output logic valid_seen,
  output logic [7:0] data_seen
);
  logic valid_async;
  logic [7:0] data_async;
  packet_source #(.WIDTH(8)) u_source (
    .src_clk(src_clk), .arst_n(arst_n), .send(send), .data_in(data_in),
    .valid_async(valid_async), .data_async(data_async)
  );
  packet_sink #(.WIDTH(8)) u_sink (
    .dst_clk(dst_clk), .arst_n(arst_n), .valid_async(valid_async),
    .data_async(data_async), .valid_seen(valid_seen), .data_seen(data_seen)
  );
endmodule
