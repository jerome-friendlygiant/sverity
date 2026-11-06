module width_leaf #(
  parameter int WIDTH = 8
) (
  input  logic [WIDTH-1:0] payload,
  output logic [WIDTH-1:0] encoded
);
  always_comb begin
    encoded = payload ^ {WIDTH{1'b1}};
  end
endmodule

module packet_wrapper #(
  parameter int WIDTH = 8
) (
  input  logic             clk,
  input  logic [WIDTH-1:0] payload,
  output logic [WIDTH-2:0] visible
);
  logic [WIDTH-2:0] narrow_encoded;
  logic [WIDTH-1:0] full_encoded;

  width_leaf #(.WIDTH(WIDTH)) u_leaf (
    .payload(payload),
    .encoded(narrow_encoded)
  );

  always_ff @(posedge clk) begin
    full_encoded <= {1'b0, narrow_encoded};
  end

  assign visible = full_encoded[WIDTH-2:0];
endmodule

module top #(
  parameter int WIDTH = 8
) (
  input  logic             clk,
  input  logic [WIDTH-1:0] payload,
  output logic [WIDTH-2:0] visible
);
  packet_wrapper #(.WIDTH(WIDTH)) u_wrapper (
    .clk(clk),
    .payload(payload),
    .visible(visible)
  );
endmodule
