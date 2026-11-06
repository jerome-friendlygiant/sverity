interface stream_if;
  logic       valid;
  logic [7:0] data;

  modport producer (
    output valid,
    output data
  );

  modport consumer (
    input valid,
    input data
  );
endinterface

module stream_source #(
  parameter logic [7:0] TAG = 8'h11
) (
  input  logic       enable,
  input  logic [7:0] payload,
  output stream_if.producer bus
);
  always_comb begin
    bus.valid = enable;
    bus.data = payload ^ TAG;
  end
endmodule

module top (
  input  logic       enable_a,
  input  logic       enable_b,
  input  logic [7:0] payload_a,
  input  logic [7:0] payload_b,
  output logic       valid,
  output logic [7:0] data
);
  stream_if stream();

  stream_source #(.TAG(8'h11)) u_source_a (
    .enable(enable_a),
    .payload(payload_a),
    .bus(stream)
  );

  stream_source #(.TAG(8'hee)) u_source_b (
    .enable(enable_b),
    .payload(payload_b),
    .bus(stream)
  );

  assign valid = stream.valid;
  assign data = stream.data;
endmodule
