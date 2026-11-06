module gray_source #(
  parameter int WIDTH = 3
) (
  input logic src_clk,
  input logic arst_n,
  input logic advance,
  output logic [WIDTH-1:0] gray_count
);
  logic [WIDTH-1:0] binary_count;
  always_ff @(posedge src_clk or negedge arst_n) begin
    if (!arst_n) begin
      binary_count <= '0;
      gray_count <= '0;
    end else if (advance) begin
      binary_count <= binary_count + 1'b1;
      // A two-bit XOR jump is not a Gray transition.
      gray_count <= gray_count ^ 3'b011;
    end
  end
endmodule

module gray_monitor #(
  parameter int WIDTH = 3
) (
  input logic dst_clk,
  input logic arst_n,
  input logic [WIDTH-1:0] gray_async,
  output logic [WIDTH-1:0] gray_seen,
  output logic jump_error
);
  logic [WIDTH-1:0] sync_one;
  logic [WIDTH-1:0] sync_two;
  always_ff @(posedge dst_clk or negedge arst_n) begin
    if (!arst_n) begin
      sync_one <= '0;
      sync_two <= '0;
      gray_seen <= '0;
      jump_error <= 1'b0;
    end else begin
      sync_one <= gray_async;
      sync_two <= sync_one;
      jump_error <= ((sync_two ^ gray_seen) & (sync_two ^ gray_seen) - 1'b1) != '0;
      gray_seen <= sync_two;
    end
  end
endmodule

module top (
  input logic src_clk,
  input logic dst_clk,
  input logic arst_n,
  input logic advance,
  output logic [2:0] gray_seen,
  output logic jump_error
);
  logic [2:0] gray_async;
  gray_source #(.WIDTH(3)) u_source (
    .src_clk(src_clk), .arst_n(arst_n), .advance(advance),
    .gray_count(gray_async)
  );
  gray_monitor #(.WIDTH(3)) u_monitor (
    .dst_clk(dst_clk), .arst_n(arst_n), .gray_async(gray_async),
    .gray_seen(gray_seen), .jump_error(jump_error)
  );
endmodule
