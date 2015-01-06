
exports.assertion = function(booleanVal, msg) {

  this.message = msg;
  this.expected = true;

  this.pass = function(value) {
    return value === this.expected;
  };

  this.failure = function(result) {
    var failed = (result === false) ||
      // no such element
      result && result.status === -1 ||
      // element doesn't have a value attribute
      result && result.value === null;
    if (failed) {
      this.message = msg;
    }
    return failed;
  };

  this.value = function(result) {
    return result;
  };

  this.command = function(callback) {
    return callback(booleanVal);
  };

};
