module ready_source #(
  parameter logic ACTIVE_LEVEL = 1'b1
) (
  input  logic       request,
  input  logic [7:0] payload,
  output logic       ready,
  output logic [7:0] data
);
  always_comb begin
    ready = request ? ACTIVE_LEVEL : ~ACTIVE_LEVEL;
    data = payload ^ 8'h5a;
  end
endmodule

module top #(
  parameter bit USE_ALT = 1'b0
) (
  input  logic       request,
  input  logic [7:0] payload,
  output logic       ready,
  output logic [7:0] data
);
  generate
    if (USE_ALT) begin : gen_alt
      ready_source #(.ACTIVE_LEVEL(1'b0)) u_source (
        .request(request),
        .payload(payload),
        .ready(ready),
        .data(data)
      );
    end else begin : gen_default
      ready_source #(.ACTIVE_LEVEL(1'b1)) u_source (
        .request(request),
        .payload(payload),
        .ready(),
        .data(data)
      );
    end
  endgenerate
endmodule
